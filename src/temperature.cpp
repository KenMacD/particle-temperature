#include <Particle.h>

#if 0
SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(AUTOMATIC);
#endif

#include "math.h"

#include <HTU21D.h>

/* Number of measurements to average across (5) */
#define AVG_ACROSS 5
/* How often to measure */
#define MEASURE_EVERY (1 * 1000)
/* How often to report (1) */
#define REPORT_EVERY 5

#if 0
SerialLogHandler logHandler(LOG_LEVEL_INFO, {
//  { "comm.protocol", LOG_LEVEL_WARN},
  { "comm.protocol", LOG_LEVEL_INFO},
  { "app", LOG_LEVEL_ALL }
});
#endif
#if 0
SerialLogHandler logHandler(LOG_LEVEL_TRACE);
#endif
SerialLogHandler logHandler(LOG_LEVEL_ALL);

int set_name(String new_name);
int get_name(String _unused);
int do_reset(String _unused);
void publish_priv(const char *event_name, const char *event_data);
void publish_priv_null(const char *event_name);
void processDelay(system_tick_t delayTime);
float calcDewpoint(float tempCelsius, float humidity);

static UDP udp;
static IPAddress remoteIP4(192, 168, 0, 1);
static const uint8_t remoteAddr[33] = {
    0x00, 0x64, 0xff, 0x9b,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    192, 168, 0, 1,
    6};
static const IPAddress remoteIP6((const HAL_IPAddress&)remoteAddr);
static int port = 8089;

HTU21D htu = HTU21D();

bool sensor_began = false;
float temps[AVG_ACROSS];
float humis[AVG_ACROSS];
int save_i;  // Location to save current measurement
int values_needed; // How many values are still needed
int report_ctr; // track if this is a report loop

uint32_t name;  // actually 4 chars

// For accessing values on the console:
static double var_temperature = 0;
static double var_humidity = 0;
static double var_dewpoint = 0;

system_tick_t last_connect;

void setup() {
    Particle.function("set_name", set_name);
    Particle.function("get_name", get_name);
    Particle.function("reset", do_reset);

    EEPROM.get(0, name);
    last_connect = millis();
    save_i = 0;
    values_needed = AVG_ACROSS;
    report_ctr = 0;

    Particle.variable("temperature", var_temperature);
    Particle.variable("humidity", var_humidity);
    Particle.variable("dewpoint", var_dewpoint);

    while (!htu.begin()) {
        Log.warn("Not connected to sensor");
        publish_priv_null("exception/sensor_needed");
        processDelay(5000);
    }
    udp.begin(0);
}

void loop() {
    Particle.process();
    float temperature, humidity, dewpoint;
    system_tick_t now = millis();
    
#if 0
    if (now > 6 * 60 * 60 * 1000UL) {
        System.reset();
    }
#endif

    if (!Particle.connected()) {
        if (now - last_connect > 300 * 1000UL) {
            System.reset();
        }

    } else {
        last_connect = now;
    }

    if (name == 0xffffffff) {
        publish_priv_null("exception/name_needed");
        processDelay(5000);
        return;
    }

    Serial.printlnf("System version: %s", System.version().c_str());
    Serial.printlnf("Ticks per uS: %d", System.ticksPerMicrosecond());

    temperature = htu.readTemperature();
    if (temperature == HTU21D_I2C_TIMEOUT || temperature == HTU21D_BAD_CRC) {
        publish_priv_null("exception/bad-temp");
        processDelay(MEASURE_EVERY);
        return;
    }

    humidity = htu.readHumidity();
    if (humidity == HTU21D_I2C_TIMEOUT || humidity == HTU21D_BAD_CRC) {
       publish_priv_null("exception/bad-humidity");
       processDelay(MEASURE_EVERY);
       return;
    }
    
    // Update the humidity based on the RH + (-0.15 * (25 - T))
    humidity += -0.15 * (25 - temperature);
    
    // Save the values:
    temps[save_i] = temperature;
    humis[save_i] = humidity;
    save_i = (save_i + 1) % AVG_ACROSS;
    report_ctr = (report_ctr + 1) % REPORT_EVERY;
    if (values_needed > 1) {
        values_needed--;
        processDelay(MEASURE_EVERY);
        return;
    }
    
    if (report_ctr == 0) {
        // Report the current values:
        temperature = 0;
        humidity = 0;
        for (int i = 0; i < AVG_ACROSS; i++) {
            temperature += temps[i];
            humidity += humis[i];
        }
        temperature /= AVG_ACROSS;
        humidity /= AVG_ACROSS;
        dewpoint = calcDewpoint(temperature, humidity);

        var_temperature = temperature;
        var_humidity = humidity;
        var_dewpoint = dewpoint;
        
        // snprintf(buf, sizeof(buf), "%.01f,%.01f,%.01f", temperature, humidity, dewpoint);
        //Particle.publish("temperature", buf, PRIVATE);
        char data[64];
        snprintf(data, sizeof(data), "%.02f", temperature);
        
        char event_name[64];
        snprintf(event_name, sizeof(event_name), "temperature/%.4s/is", (char *)&name);

        publish_priv(event_name, data);
        // Mesh.publish(event_name, data);

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "thermostat,zone=%.4s "
                "temperature=%.2f,humidity=%.2f,dewpoint=%.2f",
                (char *)&name, temperature, humidity, dewpoint);
        Serial.println(buffer);

        /* Handle both a gateway-xenon where the external server must be
         * accessed using the IPv4 address, and a mesh-xenon where the
         * IPv6 version is required.
         */
        if(network_ready(NETWORK_INTERFACE_ALL, NETWORK_READY_TYPE_IPV4, nullptr)) {
            udp.sendPacket(buffer, strlen(buffer), remoteIP4, port) < 0;
        }
        else if (network_ready(NETWORK_INTERFACE_ALL, NETWORK_READY_TYPE_IPV6, nullptr)) {
            udp.sendPacket(buffer, strlen(buffer), remoteIP6, port) < 0;
        } else {
            Particle.publish("thermostat/exception/UDPNoNetwork", buffer, PRIVATE);
        }
    }
    processDelay(MEASURE_EVERY);
    return;
}

void publish_priv(const char *event_name, const char *event_data) {
    if (Particle.connected()) {
        Particle.publish(event_name, event_data, PRIVATE);
    }
}

void publish_priv_null(const char *event_name) {
    if (Particle.connected()) {
        Particle.publish(event_name, PRIVATE);
    }
}

int get_name(String _unused) {
    char name_buf[5];
    snprintf(name_buf, sizeof(name_buf), "%.4s", (char *)&name);
    Log.info("get_name: Name: %s", name_buf);
    Particle.publish("my-name", name_buf, PRIVATE);
    return 0;
}

int set_name(String new_name) {
    if (new_name.length() != sizeof(name)) {
        return -1;
    }
    
    uint32_t *name_int = (uint32_t *)new_name.c_str();
    name = *name_int;
    
    EEPROM.put(0, name);
    
    get_name(String());
    return 0;
}

int do_reset(String _unused) {
    System.reset();
    return 0;
}

void processDelay(system_tick_t delayTime)
{
    system_tick_t start = millis();
    while (millis()-start < delayTime) {
        Particle.process();
    }
}


// https://github.com/ADiea/libDHT/blob/master/DHT.cpp
float calcDewpoint(float tempCelsius, float humidity) {
    float result;
    float percentHumidity = humidity * 0.01;
    result = 6.107799961 +
			tempCelsius * (0.4436518521 +
			tempCelsius * (0.01428945805 +
			tempCelsius * (2.650648471e-4 +
			tempCelsius * (3.031240396e-6 +
			tempCelsius * (2.034080948e-8 +
			tempCelsius * 6.136820929e-11)))));
	//Convert from mBar to kPa (1mBar = 0.1 kPa) and divide by 0.61078 constant
	//Determine vapor pressure (takes the RH into account)
	result = percentHumidity * result / (10 * 0.61078);
	result = log(result);
	result = (241.88 * result) / (17.558 - result);
	return result;
}
