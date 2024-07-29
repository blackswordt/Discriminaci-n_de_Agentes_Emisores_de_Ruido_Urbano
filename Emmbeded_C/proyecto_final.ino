#include <WiFi.h>
#include "ThingSpeak.h"
#include <esp_wpa2.h>
#include <Arduino.h>
#include "secrets.h"

#include <driver/i2s.h>
#include "simpleDSP.h" //Este es del filtro IIR
#include "FFT.h"
// you shouldn't need to change these settings
#define SAMPLE_BUFFER_SIZE 512
#define SAMPLE_RATE 8000 //Frecuencia de muestreo
// most microphones will probably default to left channel but you may need to tie the L/R pin low
#define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_LEFT
// either wire your microphone to the same pins or change these to match your wiring
#define I2S_MIC_SERIAL_CLOCK GPIO_NUM_23
#define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_22
#define I2S_MIC_SERIAL_DATA GPIO_NUM_21
#define DB_L 70
#define GN 15 
// don't mess around with this
i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// and don't mess around with this
i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA};
//Esto de abajo se le cambiará a float????
int32_t raw_samples[SAMPLE_BUFFER_SIZE]; //Aqui guardamos los datos que recibe nuestro microfono
//Aqui empieza una parte de lo del IIR con los coeficientes A B





//char ssid[] = "RedAlejandro1"; // Nombre del WiFi (nombre del router)
//char pass[] = ""; // WiFi router password

unsigned long myChannelNumber = 1950130;
const char * myWriteAPIKey = "UJ25J0M7F16N3QC9";
const char * server =" api.thingspeak.com ";

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;

WiFiClient  client;

const int Trigger = 4;   //Pin digital 2 para el Trigger del sensor
const int Echo = 2;   //Pin digital 3 para el Echo del sensor


char ssid[] = SECRET_SSID;   // your network SSID (name) 
char pass[] = SECRET_PASS;   // your network password
int keyIndex = 0;            // your network key Index number (needed only for WEP)
unsigned long weatherStationChannelNumber = SECRET_CH_ID_WEATHER_STATION;
unsigned int temperatureFieldNumber = 1;

// Counting channel details
const char * myCounterReadAPIKey = SECRET_READ_APIKEY_COUNTER;





float coefB[7] =
    {
        0.6306,
        -1.2612,
        -0.6306,
        2.5225,
        -0.6306,
        -1.2612,
        0.6306};

float coefA[7] =
    {
      1.0000,
      -2.1285,
      0.2949,
      1.8242,
      -0.8057,
      -0.3947,
      0.2099};
IIR iir1;
long startTime;
long calcTime;
    
// read from the I2S device
float previo[SAMPLE_BUFFER_SIZE];
float actual[SAMPLE_BUFFER_SIZE];
float audio_concat[SAMPLE_BUFFER_SIZE];
float fft_output[SAMPLE_BUFFER_SIZE];
float fft_input[SAMPLE_BUFFER_SIZE];
float b;
float c;

//Aqui va lo del input o arreglo pero 
void setup()
{
    // we need serial output for the plotter
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass); // Inicia WiFi
    ThingSpeak.begin(client); // Inicia ThingSpeak 
    Serial.println();
    Serial.print("Conectando a ");
    Serial.print(ssid);

    while (WiFi.status() != WL_CONNECTED)
   {
    delay(500);
    Serial.print(".");
    }

    Serial.println();
    Serial.println("Conectado a WiFi");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    




    
    // start up the I2S peripheral
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &i2s_mic_pins);
    //Esto es lo del filtro IIR
    iirInit(&iir1, 7, coefB, 7, coefA);//Inicializamos la estructura(donde_esta_almacenada y los coeficientes)
    for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++){ //checar si se puede solo con un filtro 
      previo[i]=0;
    }
}

void loop()
{
  char print_buf[300];
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, raw_samples, sizeof(int32_t) * SAMPLE_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
  int samples_read = bytes_read / sizeof(int32_t);
  //Inicialización del filtro iir 
  iirInit(&iir1, 7, coefB, 7, coefA);
  //Aqui va la fft que es inicilizada
  fft_config_t *real_fft_plan = fft_init(512, FFT_REAL, FFT_FORWARD, fft_input, fft_output);
  /*Hacemos un ciclo for donde se filtrara la señal optenida poniendo 
  la mitada de muestras pasadas y mitad de actuales*/
  for (int i = 0; i < samples_read; i++){
    actual[i]=(&iir1,raw_samples[i]);
    if (i<samples_read/2){
      //Rellenamos la mitad superior del arreglo
      audio_concat[i] = previo[i+samples_read/2];
      }
    else{
      //Rellenamos las muestras actuales, primera mitad....
      audio_concat[i] = actual[i-samples_read/2];
    }
    //Como es un proceso recursivo guardamos el valor del previo en el actual y comienza el proceso de nuevo
     previo[i] = actual[i];
     //Lllenamos el fft que se va ejecutar despues con la señal obtenida  
     real_fft_plan->input[i] = audio_concat[i];
    //delay(1);
    }
     fft_execute(real_fft_plan);
    float suma=0;
    for (int k = 1 ; k < real_fft_plan->size / 2 ; k++){
      /*The real part of a magnitude at a frequency is followed by the corresponding imaginary part in the output*/
        float mag = sqrt(pow(real_fft_plan->output[2*k],2) + pow(real_fft_plan->output[2*k+1],2))/1;
        suma = suma+pow(mag,2);
    }
    suma = suma /1 ;
    float p = ((float)1/(SAMPLE_BUFFER_SIZE/2))*suma;
    float db = 10*log(p)/5;
    if (p == 0)db = 20; //Lower limit
    //Serial.println(db);
    
    ThingSpeak.setField(2, db);
    delay(1000);
    ThingSpeak.writeFields(myChannelNumber,  myWriteAPIKey);
    //Aqui van las cosas del thingspeak donde se ploteara si db>70 
    //Arriba cree DBL que es igual a lo que  no se debe pasar que es 70 lo que hará que empieze a prog
        float temperatureInF = ThingSpeak.readFloatField(weatherStationChannelNumber, temperatureFieldNumber);  

    // Check the status of the read operation to see if it was successful
    int statusCode = ThingSpeak.getLastReadStatus();
    if(statusCode == 200)
    {
        Serial.println("Desibeles enviados desde la rasp: " + String(temperatureInF) + " dbA" );
        Serial.println("Desibeles enviados desde el esp32: " + String(db) + " dbA" );
    }
    else
    {
        Serial.println("Problem reading channel. HTTP error code " + String(statusCode)); 
    }
  
    delay(30000); // No need to read thwhile (!Serial) 

    

    
    

    //Esto de la fft destroy siempre va a estar al ultimo
    fft_destroy(real_fft_plan);
}
