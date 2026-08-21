#include <glib.h>
#include <gst/gst.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "nvmsgconv.h"
#include "nvdsmeta.h"
#include "nvdsmeta_schema.h"
#include "nvds_analytics_meta.h"


// ============================================================
// Configuration
// ============================================================

static const std::string QUEUE_ROI_NAME = "QUEUE";


// ============================================================
// Context privé
//
// Important : nvmsgconv peut appeler le générateur plusieurs
// fois pour la même frame.
//
// On mémorise donc la dernière frame envoyée par source afin
// d'éviter les messages MQTT dupliqués.
// ============================================================

struct CustomContext
{
    std::unordered_map<guint, guint64> lastFrameBySource;
    std::mutex mutex;
};


// ============================================================
// JSON escaping
// ============================================================

static std::string jsonEscape(const std::string &input)
{
    std::ostringstream out;

    for (char c : input)
    {
        switch (c)
        {
            case '"':
                out << "\\\"";
                break;

            case '\\':
                out << "\\\\";
                break;

            case '\n':
                out << "\\n";
                break;

            case '\r':
                out << "\\r";
                break;

            case '\t':
                out << "\\t";
                break;

            default:
                out << c;
                break;
        }
    }

    return out.str();
}


// ============================================================
// Timestamp UTC
// ============================================================

static std::string utcTimestamp()
{
    auto now =
        std::chrono::system_clock::now();

    std::time_t nowTime =
        std::chrono::system_clock::to_time_t(now);

    std::tm utcTime {};

    gmtime_r(
        &nowTime,
        &utcTime
    );

    std::ostringstream stream;

    stream
        << std::put_time(
            &utcTime,
            "%Y-%m-%dT%H:%M:%SZ"
        );

    return stream.str();
}


// ============================================================
// Analytics information
// ============================================================

struct ObjectAnalytics
{
    bool inQueue = false;

    std::vector<std::string> roiLabels;

    std::vector<std::string> overcrowdingLabels;

    std::vector<std::string> lineCrossingLabels;

    std::string direction;
};


// ============================================================
// Extraction NvDsAnalyticsObjInfo
// ============================================================

static ObjectAnalytics getObjectAnalytics(
    NvDsObjectMeta *objectMeta)
{
    ObjectAnalytics result;

    if (!objectMeta)
        return result;

    for (
        NvDsMetaList *userMetaList =
            objectMeta->obj_user_meta_list;

        userMetaList != nullptr;

        userMetaList =
            userMetaList->next)
    {
        NvDsUserMeta *userMeta =
            static_cast<NvDsUserMeta *>(
                userMetaList->data
            );

        if (!userMeta)
            continue;

        if (
            userMeta->base_meta.meta_type !=
            NVDS_USER_OBJ_META_NVDSANALYTICS
        )
        {
            continue;
        }

        auto *analyticsInfo =
            static_cast<NvDsAnalyticsObjInfo *>(
                userMeta->user_meta_data
            );

        if (!analyticsInfo)
            continue;


        // --------------------------------------------
        // ROI
        // --------------------------------------------

        for (
            const auto &roi :
            analyticsInfo->roiStatus)
        {
            result.roiLabels.push_back(roi);

            if (roi == QUEUE_ROI_NAME)
            {
                result.inQueue = true;
            }
        }


        // --------------------------------------------
        // Overcrowding
        // --------------------------------------------

        for (
            const auto &roi :
            analyticsInfo->ocStatus)
        {
            result.overcrowdingLabels.push_back(
                roi
            );
        }


        // --------------------------------------------
        // Line crossing
        // --------------------------------------------

        for (
            const auto &line :
            analyticsInfo->lcStatus)
        {
            result.lineCrossingLabels.push_back(
                line
            );
        }


        // --------------------------------------------
        // Direction
        // --------------------------------------------

        result.direction =
            analyticsInfo->dirStatus;
    }

    return result;
}


// ============================================================
// JSON array helper
// ============================================================

static void appendStringArray(
    std::ostringstream &json,
    const std::vector<std::string> &values)
{
    json << "[";

    for (std::size_t i = 0;
         i < values.size();
         ++i)
    {
        if (i > 0)
            json << ",";

        json
            << "\""
            << jsonEscape(values[i])
            << "\"";
    }

    json << "]";
}


// ============================================================
// Génération JSON d'une frame entière
// ============================================================

static std::string buildFrameJson(
    NvDsFrameMeta *frameMeta)
{
    std::ostringstream json;

    guint detectionCount = 0;
    guint queueCount = 0;


    // --------------------------------------------------------
    // Première passe :
    // nombre d'objets + nombre dans la queue
    // --------------------------------------------------------

    for (
        NvDsMetaList *objList =
            frameMeta->obj_meta_list;

        objList != nullptr;

        objList =
            objList->next)
    {
        auto *obj =
            static_cast<NvDsObjectMeta *>(
                objList->data
            );

        if (!obj)
            continue;

        detectionCount++;

        ObjectAnalytics analytics =
            getObjectAnalytics(obj);

        if (analytics.inQueue)
            queueCount++;
    }


    // --------------------------------------------------------
    // Header JSON
    // --------------------------------------------------------

    json
        << "{"

        << "\"timestamp\":\""
        << utcTimestamp()
        << "\","

        << "\"camera_id\":\"cam_"
        << frameMeta->source_id
        << "\","

        << "\"source_id\":"
        << frameMeta->source_id
        << ","

        << "\"frame_id\":"
        << frameMeta->frame_num
        << ","

        << "\"count\":"
        << detectionCount
        << ","

        << "\"queue_count\":"
        << queueCount
        << ","

        << "\"detections\":[";


    // --------------------------------------------------------
    // Objets
    // --------------------------------------------------------

    bool firstObject = true;

    for (
        NvDsMetaList *objList =
            frameMeta->obj_meta_list;

        objList != nullptr;

        objList =
            objList->next)
    {
        auto *obj =
            static_cast<NvDsObjectMeta *>(
                objList->data
            );

        if (!obj)
            continue;


        ObjectAnalytics analytics =
            getObjectAnalytics(obj);


        if (!firstObject)
            json << ",";

        firstObject = false;


        // --------------------------------------------
        // BBox
        // --------------------------------------------

        const float x =
            obj->rect_params.left;

        const float y =
            obj->rect_params.top;

        const float width =
            obj->rect_params.width;

        const float height =
            obj->rect_params.height;


        // --------------------------------------------
        // JSON objet
        // --------------------------------------------

        json << "{";


        // TRACK ID
        if (
            obj->object_id ==
            UNTRACKED_OBJECT_ID
        )
        {
            json
                << "\"track_id\":null,";
        }
        else
        {
            json
                << "\"track_id\":"
                << static_cast<unsigned long long>(
                    obj->object_id
                )
                << ",";
        }


        json
            << "\"class_id\":"
            << obj->class_id
            << ",";


        json
            << "\"class_name\":\""
            << jsonEscape(obj->obj_label)
            << "\",";


        json
            << "\"confidence\":"
            << std::fixed
            << std::setprecision(3)
            << obj->confidence
            << ",";


        json
            << "\"tracker_confidence\":"
            << std::fixed
            << std::setprecision(3)
            << obj->tracker_confidence
            << ",";


        // --------------------------------------------
        // Bounding box
        // --------------------------------------------

        json
            << "\"bbox\":{"

            << "\"x\":"
            << std::fixed
            << std::setprecision(3)
            << x
            << ","

            << "\"y\":"
            << y
            << ","

            << "\"width\":"
            << width
            << ","

            << "\"height\":"
            << height

            << "},";


        // --------------------------------------------
        // Queue information
        // --------------------------------------------

        json
            << "\"in_queue\":"
            << (
                analytics.inQueue
                    ? "true"
                    : "false"
            )
            << ",";


        // --------------------------------------------
        // ROI list
        // --------------------------------------------

        json << "\"roi\":";

        appendStringArray(
            json,
            analytics.roiLabels
        );

        json << ",";


        // --------------------------------------------
        // Overcrowding
        // --------------------------------------------

        json << "\"overcrowding_roi\":";

        appendStringArray(
            json,
            analytics.overcrowdingLabels
        );

        json << ",";


        // --------------------------------------------
        // Line crossing
        // --------------------------------------------

        json << "\"line_crossing\":";

        appendStringArray(
            json,
            analytics.lineCrossingLabels
        );

        json << ",";


        // --------------------------------------------
        // Direction
        // --------------------------------------------

        json
            << "\"direction\":\""
            << jsonEscape(
                analytics.direction
            )
            << "\"";


        json << "}";
    }


    json << "]}";

    return json.str();
}


// ============================================================
// Création payload
// ============================================================

static NvDsPayload *createPayload(
    const std::string &json)
{
    NvDsPayload *payload =
        static_cast<NvDsPayload *>(
            g_malloc0(
                sizeof(NvDsPayload)
            )
        );

    if (!payload)
        return nullptr;


    payload->payload =
        g_strdup(
            json.c_str()
        );


    payload->payloadSize =
        static_cast<guint>(
            json.size()
        );


    payload->componentId = 0;

    return payload;
}


// ============================================================
// API nvmsgconv
// ============================================================

extern "C"
NvDsMsg2pCtx *nvds_msg2p_ctx_create(
    const gchar *,
    NvDsPayloadType type)
{
    NvDsMsg2pCtx *ctx =
        static_cast<NvDsMsg2pCtx *>(
            g_malloc0(
                sizeof(NvDsMsg2pCtx)
            )
        );

    if (!ctx)
        return nullptr;


    ctx->payloadType = type;


    ctx->privData =
        new CustomContext();


    return ctx;
}


// ============================================================

extern "C"
void nvds_msg2p_ctx_destroy(
    NvDsMsg2pCtx *ctx)
{
    if (!ctx)
        return;


    auto *customContext =
        static_cast<CustomContext *>(
            ctx->privData
        );


    delete customContext;

    ctx->privData = nullptr;


    g_free(ctx);
}


// ============================================================
// NEW API
//
// msg-conv-msg2p-new-api=1
// ============================================================

extern "C"
NvDsPayload *nvds_msg2p_generate_new(
    NvDsMsg2pCtx *ctx,
    void *metadataInfo)
{
    if (!ctx || !metadataInfo)
        return nullptr;


    auto *info =
        static_cast<NvDsMsg2pMetaInfo *>(
            metadataInfo
        );


    auto *frameMeta =
        static_cast<NvDsFrameMeta *>(
            info->frameMeta
        );


    if (!frameMeta)
        return nullptr;


    auto *customContext =
        static_cast<CustomContext *>(
            ctx->privData
        );


    if (!customContext)
        return nullptr;


    // --------------------------------------------------------
    // Protection anti-duplication
    // --------------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            customContext->mutex
        );


        auto it =
            customContext
                ->lastFrameBySource
                .find(
                    frameMeta->source_id
                );


        if (
            it !=
                customContext
                    ->lastFrameBySource
                    .end()
            &&
            it->second ==
                frameMeta->frame_num
        )
        {
            return nullptr;
        }


        customContext
            ->lastFrameBySource[
                frameMeta->source_id
            ] =
            frameMeta->frame_num;
    }


    // --------------------------------------------------------
    // Génération frame complète
    // --------------------------------------------------------

    const std::string json =
        buildFrameJson(
            frameMeta
        );


    return createPayload(json);
}


// ============================================================
// MULTIPLE NEW API
// ============================================================

extern "C"
NvDsPayload **nvds_msg2p_generate_multiple_new(
    NvDsMsg2pCtx *ctx,
    void *metadataInfo,
    guint *payloadCount)
{
    if (!payloadCount)
        return nullptr;


    *payloadCount = 0;


    NvDsPayload *payload =
        nvds_msg2p_generate_new(
            ctx,
            metadataInfo
        );


    if (!payload)
        return nullptr;


    NvDsPayload **payloadArray =
        static_cast<NvDsPayload **>(
            g_malloc0(
                sizeof(NvDsPayload *)
            )
        );


    if (!payloadArray)
    {
        g_free(payload->payload);
        g_free(payload);

        return nullptr;
    }


    payloadArray[0] =
        payload;


    *payloadCount = 1;


    return payloadArray;
}


// ============================================================
// OLD API
//
// Non utilisée puisque nous sommes en msg2p-new-api=1,
// mais fournie pour que la bibliothèque soit complète.
// ============================================================

extern "C"
NvDsPayload *nvds_msg2p_generate(
    NvDsMsg2pCtx *,
    NvDsEvent *,
    guint)
{
    return nullptr;
}


// ============================================================

extern "C"
NvDsPayload **nvds_msg2p_generate_multiple(
    NvDsMsg2pCtx *,
    NvDsEvent *,
    guint,
    guint *payloadCount)
{
    if (payloadCount)
        *payloadCount = 0;

    return nullptr;
}


// ============================================================
// Libération payload
// ============================================================

extern "C"
void nvds_msg2p_release(
    NvDsMsg2pCtx *,
    NvDsPayload *payload)
{
    if (!payload)
        return;


    if (payload->payload)
    {
        g_free(
            payload->payload
        );
    }


    g_free(payload);
}
