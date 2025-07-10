# Breadmaker Controller Node-RED Integration Examples

This file contains example Node-RED flows for integrating with the Breadmaker Controller.

## Basic Status Monitor Flow

```json
[
  {
    "id": "breadmaker_status",
    "type": "http request",
    "z": "flow1",
    "name": "Get Breadmaker Status",
    "method": "GET",
    "ret": "obj",
    "paytoqs": "ignore",
    "url": "http://YOUR_BREADMAKER_IP/ha",
    "tls": "",
    "persist": false,
    "proxy": "",
    "authType": "",
    "x": 200,
    "y": 100,
    "wires": [["status_output"]]
  },
  {
    "id": "status_trigger",
    "type": "inject",
    "z": "flow1",
    "name": "Every 10 seconds",
    "props": [{"p": "payload"}, {"p": "topic", "vt": "str"}],
    "repeat": "10",
    "crontab": "",
    "once": true,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 130,
    "y": 100,
    "wires": [["breadmaker_status"]]
  },
  {
    "id": "status_output",
    "type": "debug",
    "z": "flow1",
    "name": "Status Debug",
    "active": true,
    "tosidebar": true,
    "console": false,
    "tostatus": false,
    "complete": "payload",
    "targetType": "msg",
    "statusVal": "",
    "statusType": "auto",
    "x": 400,
    "y": 100,
    "wires": []
  }
]
```

## Temperature Alert Flow

```json
[
  {
    "id": "temp_monitor",
    "type": "http request",
    "z": "flow2",
    "name": "Get Temperature",
    "method": "GET",
    "ret": "obj",
    "url": "http://YOUR_BREADMAKER_IP/api/temperature",
    "x": 200,
    "y": 200,
    "wires": [["temp_check"]]
  },
  {
    "id": "temp_check",
    "type": "switch",
    "z": "flow2",
    "name": "High Temp Check",
    "property": "payload.temperature",
    "propertyType": "msg",
    "rules": [
      {"t": "gt", "v": "200", "vt": "num"}
    ],
    "checkall": "true",
    "repair": false,
    "outputs": 1,
    "x": 400,
    "y": 200,
    "wires": [["temp_alert"]]
  },
  {
    "id": "temp_alert",
    "type": "email",
    "z": "flow2",
    "server": "smtp.gmail.com",
    "port": "465",
    "secure": true,
    "tls": true,
    "name": "High Temp Alert",
    "dname": "Email Alert",
    "x": 600,
    "y": 200,
    "wires": []
  }
]
```

## Program Control Flow

```json
[
  {
    "id": "start_breadmaker",
    "type": "http request",
    "z": "flow3",
    "name": "Start Breadmaker",
    "method": "GET",
    "ret": "txt",
    "url": "http://YOUR_BREADMAKER_IP/start",
    "x": 300,
    "y": 300,
    "wires": [["start_response"]]
  },
  {
    "id": "start_button",
    "type": "inject",
    "z": "flow3",
    "name": "Start Button",
    "props": [{"p": "payload"}],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "start",
    "payloadType": "str",
    "x": 120,
    "y": 300,
    "wires": [["start_breadmaker"]]
  },
  {
    "id": "stop_breadmaker",
    "type": "http request",
    "z": "flow3",
    "name": "Stop Breadmaker",
    "method": "GET",
    "ret": "txt",
    "url": "http://YOUR_BREADMAKER_IP/stop",
    "x": 300,
    "y": 340,
    "wires": [["stop_response"]]
  },
  {
    "id": "stop_button",
    "type": "inject",
    "z": "flow3",
    "name": "Stop Button",
    "props": [{"p": "payload"}],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "stop",
    "payloadType": "str",
    "x": 120,
    "y": 340,
    "wires": [["stop_breadmaker"]]
  }
]
```

## Dashboard Integration

The flows above can be integrated with Node-RED Dashboard for a custom UI:

1. Install node-red-dashboard
2. Add gauge nodes for temperature display
3. Add button nodes for control
4. Add chart nodes for temperature history

## MQTT Integration

To publish breadmaker data to MQTT:

```json
[
  {
    "id": "mqtt_publish",
    "type": "mqtt out",
    "z": "flow4",
    "name": "Publish to MQTT",
    "topic": "breadmaker/status",
    "qos": "0",
    "retain": "true",
    "broker": "mqtt_broker",
    "x": 400,
    "y": 400,
    "wires": []
  }
]
```

Replace `YOUR_BREADMAKER_IP` with your actual breadmaker IP address.
