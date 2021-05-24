
mosquitto_sub -h mqtt.meshtastic.org -v -t \$SYS/\# -t msh/+/stat/\# -t msh/+/json/\#
# mosquitto_sub -h test.mosquitto.org -v -t mesh/\# -F "%j"
