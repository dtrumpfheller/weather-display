// wifi settings
const char* ssid              = "<SSID>";                       // Wifi SSID
const char* password          = "<PASSWORD>";                   // Wifi password
const int wifiTimeout         = 15000;                          // timeout for trying to establish Wifi connection, in milliseconds
const boolean staticIP        = true;                           // if true the following IPAddress are used to configure WiFi, else DHCP will be used
const IPAddress                 localIP(192, 168, 1, 10);       // static IP, also set this in your router to avoid conflicts
const IPAddress                 gateway(192, 168, 1, 1);        // gateway, should be your routers IP
const IPAddress                 subnet(255, 255, 255, 0);       // subnet
const IPAddress                 primaryDNS(1, 1, 1, 1);         // use cloudflare as primary DNS resolver
const IPAddress                 secondaryDNS(8, 8, 8, 8);       // use google as fallback

// global properties
const String Hemisphere       = "north";                        // or "south"  
const String Units            = "M";                            // use 'M' for Metric or I for Imperial 
const char* ntpServer         = "north-america.pool.ntp.org";   // choose a time server close to you, or simply use pool.ntp.org
const int gmtOffset_sec       = 0;                              // UK normal time is GMT, so GMT Offset is 0, for US (-5Hrs) is typically -18000, AU is typically (+8hrs) 28800
const int daylightOffset_sec  = 0;                              // in the UK DST is +1hr or 3600-secs, other countries may use 2hrs 7200 or 30-mins 1800 or 5.5hrs 19800 Ahead of GMT use + offset behind - offset
const long sleepDuration      = 30;                             // sleep time in minutes, aligned to the nearest minute boundary, so if 30 will always update at 00 or 30 past the hour
const int wakeupHour          = 7;                              // wakeup after 07:00 to save battery power
const int sleepHour           = 23;                             // sleep after 23:00 to save battery power

// openweathermap.org properties
const String apikey           = "<API>";                        // OWM API key
const char server[]           = "api.openweathermap.org";
const String latitude         = "<LAT>";                        // latitude of place for weather data, use google maps to get your specific geo address
const String longitude        = "<LONG>";                       // longitude of place for weather data
const String language         = "EN";                           // only the weather description is translated by OWM

// influxdb2 agent properties
const String influxDb2Agent   = "<URL>";                        // base url of InfluxDB2-Agent
const int historyDays         = 3;                              // number of days the history data covers