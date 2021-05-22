#include <algorithm>
#include <pistache/net.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/http_headers.h>
#include <pistache/cookie.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>
#include <pistache/common.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

using namespace std;
using namespace Pistache;

// General advice: pay atetntion to the namespaces that you use in various contexts. Could prevent headaches.

// This is just a helper function to preety-print the Cookies that one of the enpoints shall receive.
void printCookies(const Http::Request& req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto& c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic {

    void handleReady(const Rest::Request&, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "1");
    }

}

// Definition of the MicrowaveEnpoint class 
class CupThorEndpoint {
public:
    explicit CupThorEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    // Initialization of the server. Additional options can be provided here
    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options()
            .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    // Server is started threaded.  
    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    // When signaled server shuts down
    void stop(){
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;
        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called. 
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&CupThorEndpoint::doAuth, this));
        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&CupThorEndpoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&CupThorEndpoint::getSetting, this));
    }

    
    void doAuth(const Rest::Request& request, Http::ResponseWriter response) {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok);
    }

    // Endpoint to configure one of the Microwave's settings.
    void setSetting(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        auto settingName = request.param(":settingName").as<std::string>();

        // This is a guard that prevents editing the same value by two concurent threads. 
        Guard guard(cupthorLock);

        
        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<string>();
        }

        // Setting the microwave's setting to value
        int setResponse = cth.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, settingName + " was set to " + val);
        }
        else if(setResponse == 2){
            if (val == "true")
                response.send(Http::Code::Ok, "Silent mode is activated. \nAmbient light is turned off and ventilation is set to at most 2.");
            else 
                response.send(Http::Code::Ok, "Silent mode is deactivated. \nAmbient light is turned on and ventilation is unchanged.");

        }
        else if(setResponse == 3){
            response.send(Http::Code::Ok, "Silent mode is activated! \nTurn it off and try again.");
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found and or '" + val + "' was not a valid value ");
        }

    }

    // Setting to get the settings value of one of the configurations of the Microwave
    void getSetting(const Rest::Request& request, Http::ResponseWriter response){
        auto settingName = request.param(":settingName").as<std::string>();

        Guard guard(cupthorLock);

        string valueSetting = cth.get(settingName);

        if (valueSetting != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                        .add<Header::Server>("pistache/0.1")
                        .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found");
        }
    }

    // Defining the class of the Microwave. It should model the entire configuration of the Microwave
    class CupThor {
    public:
        explicit CupThor(){ }

        // Setting the value for one of the settings. Hardcoded for the defrosting option
        int set(std::string name, std::string value){
            if(name == "defrost"){
                defrost.name = name;
                if(value == "true"){
                    defrost.value = true;
                    return 1;
                }
                if(value == "false"){
                    defrost.value = false;
                    return 1;
                }
            }



            if(name == "temperature"){
                temperature.name = name;

                float valoare;
                valoare = std::stof(value);


                if (valoare <= 300 || valoare >= 0)
                {
                    temperature.value = valoare;
                    return 1;   
                }
                
            }

            if (name == "ambient_light"){
                ambient_light.name = name;

                if (value == "true"){
                    if(silent_mode.value)
                    {
                        ambient_light.value = false;
                        return 3;
                    }
                     
                    else
                    {
                    ambient_light.value = true;
                    return 1;
                    }
                }
                
                if (value == "false"){
                    ambient_light.value = false;
                    return 1;
                }
            }


            if (name == "ventilation"){
                ventilation.name = name;

                int valoare;
                valoare = std::stoi(value);

                if (valoare >= 0 && valoare <=6){
                    if (silent_mode.value && valoare > 2)
                        return 3;
                    else
                    {
                    ventilation.value = valoare;
                    return 1;
                    }
                }
            }

            if (name == "silent_mode"){
                silent_mode.name = name;

                if (value == "true"){
                    silent_mode.value = true;
                    ambient_light.value = false;
                    if (ventilation.value > 2)
                        ventilation.value = 2;

                    return 2;
                }
                
                if (value == "false"){
                    silent_mode.value = false;
                    ambient_light.value = true;
                    return 2;
                }
            }

            return 0;
        }

        // Getter
        string get(std::string name){
            if (name == "defrost"){
                return std::to_string(defrost.value);
            }


            else if (name == "temperature"){
                return std::to_string(temperature.value);
            }

            else if (name == "ambient_light"){
                return std::to_string(ambient_light.value);
            }

            else if (name == "ventilation"){
                return std::to_string(ventilation.value);
            }

            else if (name == "silent_mode"){
                return std::to_string(silent_mode.value);
            }

            else{
                return "";
            }
        }

    private:
        // Defining and instantiating settings.
        struct boolSetting{
            std::string name;
            bool value;
        }defrost;


        struct temperatureSetting{
            std::string name;
            float value;
        }temperature;


        struct ambient_lightSetting{
            std::string name;
            bool value;
        }ambient_light;


        struct ventilationSetting{
            std::string name;
            int value;
        }ventilation;


        struct silent_modeSetting{
            std::string name;
            bool value;
        }silent_mode;
    };

    // Create the lock which prevents concurrent editing of the same variable
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock cupthorLock;

    // Instance of the microwave model
    CupThor cth;

    // Defining the httpEndpoint and a router.
    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char *argv[]) {

    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0
            || sigaddset(&signals, SIGTERM) != 0
            || sigaddset(&signals, SIGINT) != 0
            || sigaddset(&signals, SIGHUP) != 0
            || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    // Instance of the class that defines what the server can do.
    CupThorEndpoint stats(addr);

    // Initialize and start the server
    stats.init(thr);
    stats.start();


    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}