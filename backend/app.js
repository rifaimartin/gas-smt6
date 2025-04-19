const express = require('express');
const cors = require('cors');
const { Kafka } = require('kafkajs');
const mongoose = require('mongoose');
const path = require('path');

// Environment variables
const PORT = process.env.PORT || 3000;
const MONGODB_URI = process.env.MONGODB_URI;
const KAFKA_BROKER = process.env.KAFKA_BROKER || 'gas-smt6.railway.internal:9092';

// Initialize Express
const app = express();
app.use(cors());
app.use(express.json());
// Coba menggunakan path absolut
app.use(express.static(path.join(__dirname, 'public')));

// Dan tambahkan route khusus untuk root path
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// Connect to MongoDB
mongoose.connect(MONGODB_URI)
  .then(() => console.log('Connected to MongoDB'))
  .catch(err => console.error('MongoDB connection error:', err));

// Define Sensor Data Schema
const sensorDataSchema = new mongoose.Schema({
  device_id: { type: String, required: true },
  raw_value: Number,
  voltage: Number,
  resistance: Number,
  ratio: Number,
  alcohol_ppm: { type: Number, required: true },
  timestamp: { type: Date, default: Date.now },
  received_at: { type: Date, default: Date.now }
});

const SensorData = mongoose.model('SensorData', sensorDataSchema);

// Kafka Configuration
const kafka = new Kafka({
  clientId: 'gas-sensor-consumer',
  brokers: [KAFKA_BROKER]
});

const consumer = kafka.consumer({ groupId: 'gas-sensor-group' });

// Start Kafka Consumer
const runConsumer = async () => {
  try {
    await consumer.connect();
    console.log('Connected to Kafka');
    
    await consumer.subscribe({ topic: 'gas-sensor-readings', fromBeginning: false });
    
    await consumer.run({
      eachMessage: async ({ topic, partition, message }) => {
        try {
          const data = JSON.parse(message.value.toString());
          console.log(`Received message: ${JSON.stringify(data)}`);
          
          // Store in MongoDB
          const sensorData = new SensorData({
            device_id: data.device_id,
            raw_value: data.raw_value,
            voltage: data.voltage,
            resistance: data.resistance,
            ratio: data.ratio,
            alcohol_ppm: data.alcohol_ppm,
            timestamp: new Date(parseInt(data.timestamp)),
          });
          
          await sensorData.save();
          console.log(`Data saved with ID: ${sensorData._id}`);
        } catch (err) {
          console.error('Error processing message:', err);
        }
      },
    });
  } catch (err) {
    console.error('Error running Kafka consumer:', err);
  }
};

// API Routes
app.get('/api/sensor-data', async (req, res) => {
  try {
    const { limit = 100, from, to } = req.query;
    
    const query = {};
    if (from || to) {
      query.timestamp = {};
      if (from) query.timestamp.$gte = new Date(from);
      if (to) query.timestamp.$lte = new Date(to);
    }
    
    const data = await SensorData.find(query)
      .sort({ timestamp: -1 })
      .limit(parseInt(limit))
      .exec();
      
    res.json(data);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/statistics', async (req, res) => {
  try {
    const stats = await SensorData.aggregate([
      {
        $group: {
          _id: null,
          avgPpm: { $avg: "$alcohol_ppm" },
          maxPpm: { $max: "$alcohol_ppm" },
          minPpm: { $min: "$alcohol_ppm" },
          count: { $sum: 1 }
        }
      }
    ]);
    
    res.json(stats[0] || { avgPpm: 0, maxPpm: 0, minPpm: 0, count: 0 });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Start server
app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
  // Start consumer
  runConsumer().catch(console.error);
});