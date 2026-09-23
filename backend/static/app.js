const queueLength = document.getElementById("queue-length");
const inService = document.getElementById("in-service");
const averageWait = document.getElementById("average-wait");
const longestWait = document.getElementById("longest-wait");
const averageService = document.getElementById("average-service");
const servedHour = document.getElementById("served-hour");
const serviceEfficiency = document.getElementById("service-efficiency");
const serviceLevel = document.getElementById("service-level");
const abandoned = document.getElementById("abandoned");

const queueStatus = document.getElementById("queue-status");
const statusIndicator = document.getElementById("status-indicator");
const statusText = document.getElementById("status-text");

const customersTable = document.getElementById("customers");
const connection = document.getElementById("connection");
const customerCount = document.getElementById("customer-count");
const lastUpdate = document.getElementById("last-update");


function connectWebSocket() {

    const protocol =
        window.location.protocol === "https:"
            ? "wss"
            : "ws";

    const socket =
        new WebSocket(
            `${protocol}://${window.location.host}/ws/queue`
        );

    socket.onopen = () => {

        connection.className =
            "connection connected";

        connection.innerHTML =
            `
            <span class="status-dot"></span>
            LIVE
            `;
    };


    socket.onmessage = event => {

        const data =
            JSON.parse(event.data);

        updateDashboard(data);
    };


    socket.onclose = () => {

        connection.className =
            "connection disconnected";

        connection.innerHTML =
            `
            <span class="status-dot"></span>
            OFFLINE
            `;

        setTimeout(
            connectWebSocket,
            1500
        );
    };


    socket.onerror = () => {
        socket.close();
    };
}


function formatSeconds(value) {

    const seconds =
        Number(value || 0);

    if (seconds < 60) {
        return `${seconds.toFixed(1)} s`;
    }

    const minutes =
        Math.floor(seconds / 60);

    const remaining =
        Math.round(seconds % 60);

    return `${minutes}m ${remaining}s`;
}


function updateDashboard(data) {

    const length =
        data.queue_length ?? 0;

    const serving =
        data.customers_in_service ?? 0;

    const average =
        data.average_waiting_time ?? 0;

    const longest =
        data.longest_waiting_time ?? 0;

    const avgService =
        data.average_service_time ?? 0;

    const servedLastHour =
        data.customers_served_last_hour ?? 0;

    const efficiency =
        data.service_efficiency ?? 100;

    const level =
        data.service_level ?? 100;

    const abandonedCount =
        data.abandoned_customers ?? 0;

    const status =
        data.status || "normal";


    queueLength.textContent =
        length;

    inService.textContent =
        serving;

    averageWait.textContent =
        formatSeconds(average);

    longestWait.textContent =
        formatSeconds(longest);

    averageService.textContent =
        formatSeconds(avgService);

    servedHour.textContent =
        servedLastHour;

    serviceEfficiency.textContent =
        `${Number(efficiency).toFixed(1)}%`;

    serviceLevel.textContent =
        `${Number(level).toFixed(1)}%`;

    abandoned.textContent =
        abandonedCount;


    const activeCount =
        (data.customers || []).length;

    customerCount.textContent =
        `${activeCount} active`;


    queueStatus.textContent =
        status.toUpperCase();

    queueStatus.className =
        `queue-status ${status}`;

    statusIndicator.className =
        `large-status-dot ${status}`;


    if (status === "normal") {

        statusText.textContent =
            "Operations normal";

    }
    else if (status === "busy") {

        statusText.textContent =
            "Queue demand increasing";

    }
    else {

        statusText.textContent =
            "Immediate attention required";
    }


    lastUpdate.textContent =
        new Date().toLocaleTimeString();


    renderCustomers(
        data.customers || []
    );
}


function renderCustomers(customers) {

    customersTable.innerHTML =
        "";


    if (!customers.length) {

        customersTable.innerHTML =
            `
            <tr>
                <td colspan="5" class="empty-state">
                    No active customers
                </td>
            </tr>
            `;

        return;
    }


    customers.forEach(customer => {

        const row =
            document.createElement("tr");

        const status =
            customer.status || "waiting";

        const statusLabel =
            status === "serving"
                ? "IN SERVICE"
                : "WAITING";


        row.innerHTML =
            `
            <td>
                <strong>
                    #${customer.track_id}
                </strong>
            </td>

            <td>
                <span class="customer-status ${status}">
                    ${statusLabel}
                </span>
            </td>

            <td>
                ${formatSeconds(customer.waiting_time)}
            </td>

            <td>
                ${
                    status === "serving"
                        ? formatSeconds(customer.service_time)
                        : "—"
                }
            </td>

            <td>
                ${
                    (
                        Number(
                            customer.confidence || 0
                        )
                        * 100
                    ).toFixed(1)
                } %
            </td>
            `;


        customersTable.appendChild(
            row
        );
    });
}


connectWebSocket();
