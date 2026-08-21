#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "nvdsinfer_custom_impl.h"

static float clampf(float value, float min_value, float max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

extern "C"
bool NvDsInferParseCustomYoloV12(
    std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
    NvDsInferNetworkInfo const &networkInfo,
    NvDsInferParseDetectionParams const &detectionParams,
    std::vector<NvDsInferObjectDetectionInfo> &objectList)
{
    if (outputLayersInfo.empty())
    {
        std::cerr << "YOLOv12 parser: aucune couche de sortie." << std::endl;
        return false;
    }

    const NvDsInferLayerInfo *outputLayer = nullptr;

    for (const auto &layer : outputLayersInfo)
    {
        if (layer.layerName &&
            std::string(layer.layerName) == "output0")
        {
            outputLayer = &layer;
            break;
        }
    }

    if (!outputLayer)
    {
        std::cerr
            << "YOLOv12 parser: output0 introuvable."
            << std::endl;

        return false;
    }

    if (!outputLayer->buffer)
    {
        std::cerr
            << "YOLOv12 parser: buffer output0 null."
            << std::endl;

        return false;
    }

    const float *output =
        static_cast<const float *>(outputLayer->buffer);

    /*
     * Tensor:
     *
     * [1, 84, 8400]
     *
     * 84 =
     *   4 bbox
     *   +
     *   80 classes
     *
     * Layout BCN:
     *
     * channel 0 = center_x
     * channel 1 = center_y
     * channel 2 = width
     * channel 3 = height
     *
     * channel 4..83 = class scores
     */

    const unsigned int numClasses =
        detectionParams.numClassesConfigured;

    if (numClasses == 0)
    {
        std::cerr
            << "YOLOv12 parser: numClassesConfigured = 0."
            << std::endl;

        return false;
    }

    const unsigned int channels =
        4 + numClasses;

    const unsigned int totalElements =
        outputLayer->inferDims.numElements;

    if (totalElements % channels != 0)
    {
        std::cerr
            << "YOLOv12 parser: dimensions incompatibles."
            << " totalElements=" << totalElements
            << " channels=" << channels
            << std::endl;

        return false;
    }

    const unsigned int numCandidates =
        totalElements / channels;

    /*
     * Pour ton moteur:
     *
     * channels      = 84
     * numCandidates = 8400
     */

    objectList.clear();
    objectList.reserve(numCandidates);

    for (unsigned int i = 0;
         i < numCandidates;
         ++i)
    {
        const float cx =
            output[0 * numCandidates + i];

        const float cy =
            output[1 * numCandidates + i];

        const float width =
            output[2 * numCandidates + i];

        const float height =
            output[3 * numCandidates + i];

        int bestClassId = -1;
        float bestScore = 0.0f;

        for (unsigned int classId = 0;
             classId < numClasses;
             ++classId)
        {
            const float score =
                output[
                    (4 + classId)
                    * numCandidates
                    + i
                ];

            if (score > bestScore)
            {
                bestScore = score;
                bestClassId =
                    static_cast<int>(classId);
            }
        }

        if (bestClassId < 0)
            continue;

        float threshold = 0.25f;

        if (bestClassId <
            static_cast<int>(
                detectionParams
                    .perClassPreclusterThreshold
                    .size()))
        {
            threshold =
                detectionParams
                    .perClassPreclusterThreshold[
                        bestClassId
                    ];
        }

        if (bestScore < threshold)
            continue;

        /*
         * YOLO xywh -> DeepStream xyxy/left-top-width-height
         */

        float left =
            cx - width / 2.0f;

        float top =
            cy - height / 2.0f;

        float right =
            cx + width / 2.0f;

        float bottom =
            cy + height / 2.0f;

        left = clampf(
            left,
            0.0f,
            static_cast<float>(networkInfo.width - 1));

        top = clampf(
            top,
            0.0f,
            static_cast<float>(networkInfo.height - 1));

        right = clampf(
            right,
            0.0f,
            static_cast<float>(networkInfo.width - 1));

        bottom = clampf(
            bottom,
            0.0f,
            static_cast<float>(networkInfo.height - 1));

        float boxWidth =
            right - left;

        float boxHeight =
            bottom - top;

        if (boxWidth <= 0.0f ||
            boxHeight <= 0.0f)
        {
            continue;
        }

        NvDsInferObjectDetectionInfo object;

        object.classId =
            static_cast<unsigned int>(
                bestClassId);

        object.detectionConfidence =
            bestScore;

        object.left =
            left;

        object.top =
            top;

        object.width =
            boxWidth;

        object.height =
            boxHeight;

        objectList.push_back(object);
    }

    return true;
}

CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(
    NvDsInferParseCustomYoloV12
);
