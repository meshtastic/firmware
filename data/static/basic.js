var meshtasticClient;
var connectionOne;


// Important: the connect action must be called from a user interaction (e.g. button press), otherwise the browsers won't allow the connect
function connect() {

    // Create new connection
    var httpconn = new meshtasticjs.IHTTPConnection();

    // Set connection params
    let sslActive;
    if (window.location.protocol === 'https:') {
        sslActive = true;
    } else {
        sslActive = false;
    }
    let deviceIp = window.location.hostname; // Your devices IP here


    // Add event listeners that get called when a new packet is received / state of device changes
    httpconn.addEventListener('fromRadio', function (packet) { console.log(packet) });

    // Connect to the device async, then send a text message
    httpconn.connect(deviceIp, sslActive)
        .then(result => {

            alert('device has been configured')
            // This gets called when the connection has been established
            // -> send a message over the mesh network. If no recipient node is provided, it gets sent as a broadcast
            return httpconn.sendText('meshtastic is awesome');

        })
        .then(result => {

            // This gets called when the message has been sucessfully sent
            console.log('Message sent!');
        })

        .catch(error => { console.log(error); });

}

