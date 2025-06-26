// test_status_endpoint.js
// JavaScript test for querying the live /status endpoint on the device
const http = require('http');
const DEVICE_URL = '192.168.250.125';

describe('Live /status endpoint', () => {
  it('should return valid status JSON', (done) => {
    http.get({host: DEVICE_URL, path: '/status', port: 80}, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        try {
          const json = JSON.parse(data);
          expect(json).toHaveProperty('stageStartTimes');
          expect(json).toHaveProperty('stageReadyAt');
          expect(json).toHaveProperty('programReadyAt');
          done();
        } catch (e) {
          done(e);
        }
      });
    }).on('error', done);
  });
});
