var meshtasticClient;
var connectionOne;


// run init when DOM is ready
document.addEventListener("DOMContentLoaded", function() {

    // Create new client instance
    meshtasticClient = new meshtasticjs.Client;

});


// Important: the connect action must be called from a user interaction (e.g. button press), otherwise the browsers won't allow the connect
function connect() {

    // Create new connection
    connectionOne = meshtasticClient.createBLEConnection();

    // Add event listeners that get called when a new packet is received / state of device changes
    connectionOne.addEventListener('dataPacket', function(packet) { console.log(packet)});
    connectionOne.addEventListener('userPacket', function(packet) { console.log(packet)});
    connectionOne.addEventListener('positionPacket', function(packet) { console.log(packet)});
    connectionOne.addEventListener('connected', function() { console.log('connected!')});
    connectionOne.addEventListener('disconnected', function() { console.log('disconnected!')});

    // Connect to the device async, then send a text message
    connectionOne.connect()
    .then(result => { 

        // This gets called when the connection has been established
        // -> send a message over the mesh network. If no recipient node is provided, it gets sent as a broadcast
        return connectionOne.sendText('meshtastic is awesome');

    })
    .then(result => { 

        // This gets called when the message has been sucessfully sent
        console.log('Message sent!');})

    .catch(error => { console.log(error); });

}








