const queueLength = document.getElementById("queue-length");
const averageWait = document.getElementById("average-wait");
const longestWait = document.getElementById("longest-wait");
const queueStatus = document.getElementById("queue-status");
const statusIndicator = document.getElementById("status-indicator");
const statusText = document.getElementById("status-text");
const customersTable = document.getElementById("customers");
const connection = document.getElementById("connection");
const customerCount = document.getElementById("customer-count");
const lastUpdate = document.getElementById("last-update");

// L'URL WebRTC est définie directement dans index.html.
// On ne modifie plus iframe.src en JavaScript.

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

function updateDashboard(data) {

    const length =
        data.queue_length ?? 0;

    const average =
        data.average_waiting_time ?? 0;

    const longest =
        data.longest_waiting_time ?? 0;

    const status =
        data.status || "normal";

    queueLength.textContent = length;
    averageWait.textContent = `${average} s`;
    longestWait.textContent = `${longest} s`;
    customerCount.textContent = `${length} active`;

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

    customersTable.innerHTML = "";

    if (!customers.length) {

        customersTable.innerHTML =
            `
            <tr>
                <td colspan="4" class="empty-state">
                    No customers currently waiting
                </td>
            </tr>
            `;

        return;
    }

    customers
        .sort(
            (a, b) =>
                b.waiting_time -
                a.waiting_time
        )
        .forEach(customer => {

            const row =
                document.createElement("tr");

            row.innerHTML =
                `
                <td>
                    <strong>
                        #${customer.track_id}
                    </strong>
                </td>

                <td>
                    <span class="customer-waiting">
                        WAITING
                    </span>
                </td>

                <td>
                    ${customer.waiting_time} s
                </td>

                <td>
                    ${
                        (
                            customer.confidence *
                            100
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
