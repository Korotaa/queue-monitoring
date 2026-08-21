const queueLength =
    document.getElementById(
        "queue-length"
    );

const averageWait =
    document.getElementById(
        "average-wait"
    );

const longestWait =
    document.getElementById(
        "longest-wait"
    );

const queueStatus =
    document.getElementById(
        "queue-status"
    );

const customersTable =
    document.getElementById(
        "customers"
    );

const connection =
    document.getElementById(
        "connection"
    );


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

        connection.textContent =
            "LIVE";

        connection.className =
            "connection connected";
    };


    socket.onmessage = event => {

        const data =
            JSON.parse(
                event.data
            );

        updateDashboard(
            data
        );
    };


    socket.onclose = () => {

        connection.textContent =
            "OFFLINE";

        connection.className =
            "connection disconnected";

        setTimeout(
            connectWebSocket,
            2000
        );
    };


    socket.onerror = () => {

        socket.close();
    };
}


function updateDashboard(data) {

    queueLength.textContent =
        data.queue_length ?? 0;


    averageWait.textContent =
        `${data.average_waiting_time ?? 0} s`;


    longestWait.textContent =
        `${data.longest_waiting_time ?? 0} s`;


    const status =
        data.status || "normal";


    queueStatus.textContent =
        status.toUpperCase();


    queueStatus.className =
        `status ${status}`;


    customersTable.innerHTML =
        "";


    for (
        const customer
        of data.customers || []
    ) {

        const row =
            document.createElement(
                "tr"
            );


        row.innerHTML = `

            <td>
                ${customer.track_id}
            </td>

            <td>
                ${customer.waiting_time} s
            </td>

            <td>
                ${customer.confidence}
            </td>
        `;


        customersTable.appendChild(
            row
        );
    }
}


connectWebSocket();
