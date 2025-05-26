require('dotenv').config();
const express = require('express');
const mqtt = require('mqtt');
const cors = require('cors');
// const fs = require('fs');
const { InfluxDB, Point } = require('@influxdata/influxdb-client');
// const { url } = require('inspector');

const app = express();
const port = 3000;

// Middleware
app.use(express.json());
app.use(cors());

// MQTT setup
const mqttOptions = {
    host: process.env.MQTT_BROKER_HOST,
    port: process.env.MQTT_BROKER_PORT,
    protocol: 'mqtts',
    ca: process.env.MQTT_BROKER_CA,
    rejectUnauthorized: true,
    username: process.env.MQTT_BROKER_USERNAME,
    password: process.env.MQTT_BROKER_PASSWORD,
};

const mqttClient = mqtt.connect(mqttOptions);

mqttClient.on('connect', () => {
    console.log('✅ MQTT Connected');
    mqttClient.subscribe('home/living_room/+/status');
});

mqttClient.on('error', (err) => {
    console.error('❌ MQTT Error:', err);
});

// InfluxDB setup
const influxDB = new InfluxDB({
    url: process.env.INFLUX_URL,
    token: process.env.INFLUX_TOKEN
});
const org = process.env.INFLUX_ORG;
const bucket = process.env.INFLUX_BUCKET;
const writeApi = influxDB.getWriteApi(org, bucket, 'ns');
writeApi.useDefaultTags({ location: 'home' });
const queryApi = influxDB.getQueryApi(org);

// MQTT message handler
mqttClient.on('message', (topic, message) => {
    try {
        const data = JSON.parse(message.toString());
        console.log(`[MQTT] Topic: ${topic}`);
        console.log(`[MQTT] Message: ${message}`);

        if (!data.device_id || !data.values) {
            console.warn('[⚠️ MQTT] Missing device_id or values:', data);
            return;
        }

        const point = new Point('sensor_data')
            .tag('device', data.device_id)
            .tag('location', data.location || 'unknown')
            .tag('type', data.type || 'unknown');

        const values = data.values;
        for (const [key, value] of Object.entries(values)) {
            if (typeof value === 'number') {
                point.floatField(key, value);
            }
        }

        if (data.timestamp) {
            // TODO:
            // point.timestamp(data.timestamp);
            point.timestamp(new Date());
        }
        writeApi.writePoint(point);
        console.log(`✅ Data written to InfluxDB from [${data.device_id}]`);
    } catch (err) {
        console.error('[❌ Error parsing MQTT message]', err.message);
    }
});

// ========== LẤY SENSOR MỚI NHẤT ==========

app.get('/api/sensor/:type/latest', async (req, res) => {
    const sensorType = req.params.type; // dht_sensor | ntc_sensor | soil_sensor ...
    const queryApi = influxDB.getQueryApi(org);

    const fluxQuery = `
      from(bucket:"${bucket}")
        |> range(start: -1h)
        |> filter(fn: (r) => r._measurement == "sensor_data")
        |> filter(fn: (r) => r.type == "${sensorType}")
        |> sort(columns: ["_time"], desc: true)
        |> limit(n: 1)
    `;

    const results = {};

    queryApi.queryRows(fluxQuery, {
        next(row, tableMeta) {
            const o = tableMeta.toObject(row);
            const key = `${o.device || 'unknown'}_${o._field}`;
            if (!results[o.device]) {
                results[o.device] = {
                    device_id: o.device,
                    type: o.type,
                    timestamp: o._time,
                    values: {},
                };
            }
            results[o.device].values[o._field] = o._value;
        },
        complete() {
            res.json(Object.values(results));
        },
        error(error) {
            console.error('[InfluxDB]', error.message);
            res.status(500).send('Error querying data');
        }
    });
});


// ========== ĐIỀU KHIỂN THIẾT BỊ ==========
// controlType: light | pump | fan | ...
// status: on | off

app.post('/api/device/:deviceId/control/:controlType', (req, res) => {
    const { deviceId, controlType } = req.params;
    const { status } = req.body;

    if (!['on', 'off'].includes(status)) {
        return res.status(400).json({ error: 'Invalid status. Use "on" or "off".' });
    }

    const topic = `home/living_room/${deviceId}/control`; // or dynamic room
    const payload = { [controlType]: status };

    mqttClient.publish(topic, JSON.stringify(payload));
    console.log(`[MQTT] Sent control to ${deviceId}:`, payload);

    res.json({ message: `${controlType} on ${deviceId} turned ${status}` });
});

// Khởi động server
app.listen(port, () => {
    console.log(`🚀 Server listening at http://localhost:${port}`);
});
