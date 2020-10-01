/***************************************
* httpsocket.c
*
* SER486 Final Project
* Fall 2019
* Written By: Rebecca Rodriguez
* Modified By:
*
* This file contains function definitions for parsing
* and HTTP protocol functionality.
*/

#include "httpsocket.h"
#include "socket.h"
#include "uart.h"
#include "vpd.h"
#include "wdt.h"
#include "config.h"
#include "log.h"
#include "temp.h"
#include "rtc.h"

#define HTTP_PORT 8080	/* TCP port for HTTP */

//Enum of parsing HTTP request states
enum state
{
    WAIT,
    READ_REQUEST,
    READ_HEADERS,
    READ_BODY,
    REPLY
};
enum state current_state = WAIT;

//HTTP request types
enum requestLine {GET, PUT, RESET, DELETE, ERROR};
enum requestLine request;

static void update_value(unsigned char socketNum);
static void write_vpd(unsigned char socketNum);
static void write_temp_values(unsigned char socketNum);
static void write_log(unsigned char socketNum);

/**********************************
* httpsocket_update(unsigned char socketNum)
*
* This code creates a FSM that works directly
* with an HTTP socket
*
* arguments:
* unsigned char - the specified server socket
*
* returns:
* nothing
*
* changes:
* State of HTTP socket, contents of EEPROM config
*/

void httpsocket_update(unsigned char socketNum)
{

    //If server socket is closed
    if (socket_is_closed(socketNum))
    {
        socket_open(socketNum, HTTP_PORT);
        socket_listen(socketNum);
    }

    else
    {

        //Process pending commands
        switch(current_state)
        {

        case WAIT:
        {

            if(socket_received_line(socketNum))
            {
                current_state = READ_REQUEST;
            }
        }

        break;

        case READ_REQUEST:
        {

            //Get request line, set match
            if (socket_recv_compare(socketNum, "GET"))
            {

                if (socket_recv_compare(socketNum, " /device HTTP/1.1"))
                {
                    request = GET;
                }
                else
                {
                    request = ERROR;
                }
            }

            else if (socket_recv_compare(socketNum, "PUT"))
            {

                request = PUT;

                //If PUT is reset or temp
                if (socket_recv_compare(socketNum, " /device/config?"))
                {
                    update_value(socketNum);
                }

                else if (socket_recv_compare(socketNum, " /device?reset=\"true\""))
                {

                    request = RESET;

                }

                else
                {

                    request = ERROR;

                }


            }

            else if (socket_recv_compare(socketNum, "DELETE"))
            {

                //If logs need to be deleted
                if (socket_recv_compare(socketNum, " /device/log HTTP/1.1"))
                {

                    request = DELETE;
                    log_clear();
                }

                else
                {
                    request = ERROR;
                }
            }

            else
            {

                request = ERROR;

            }

            //Remove rest of message
            socket_flush_line(socketNum);
            current_state = READ_HEADERS;
        }

        break;

        case READ_HEADERS:
        {

            //Get entire line into buffer
            if (socket_received_line(socketNum))
            {

                //If end of header has been reached
                if (socket_is_blank_line(socketNum))
                {
                    current_state = READ_BODY;
                }

                else
                {

                    socket_flush_line(socketNum);

                }
            }
        }

        break;

        case READ_BODY:
        {

            //If end of header has been reached
            if (socket_is_blank_line(socketNum))
            {
                current_state = REPLY;
            }

            socket_flush_line(socketNum);

        }

        break;

        case REPLY:
        {

            //Send device data
            if (request == GET)
            {
                socket_writestr(socketNum, "HTTP/1.1 200 OK\r\n");
                socket_writestr(socketNum, "Content-Type: application/vnd.api+json\r\n");
                socket_writestr(socketNum, "Connection: close\r\n");
                socket_writestr(socketNum, "\r\n");

                write_vpd(socketNum);

                socket_writechar(socketNum, ',');

                write_temp_values(socketNum);

                socket_writechar(socketNum, ',');
                socket_writequotedstring(socketNum, "temperature");
                socket_writechar(socketNum, ':');
                socket_writedec32(socketNum, temp_get());
                socket_writechar(socketNum, ',');
                socket_writequotedstring(socketNum, "state");
                socket_writechar(socketNum, ':');
                socket_writequotedstring(socketNum, "NORMAL");
                socket_writechar(socketNum, ',');

                write_log(socketNum);

                socket_writestr(socketNum, "}\r\n");
                socket_writestr(socketNum, "\r\n");

            }

            else if (request == ERROR)
            {

                socket_writestr(socketNum, "HTTP/1.1 400 Bad Request\r\n");
                socket_writestr(socketNum, "Connection: close\r\n");
                socket_writestr(socketNum, "\r\n");

            }

            //Standard reply for other requests
            else
            {

                socket_writestr(socketNum, "HTTP/1.1 200 OK\r\n");
                socket_writestr(socketNum, "Connection: close\r\n");
                socket_writestr(socketNum, "\r\n");

            }

            socket_flush_line(socketNum);
            socket_disconnect(socketNum);

            if (request == RESET)
            {

                wdt_force_restart();

            }
            current_state = WAIT;

        }

        break;
        }

    }
}

/**********************************
* update_value(unsigned char socketNum)
*
* This code updates specified config values
* based on the HTTP request
*
* arguments:
* unsigned char - the specified server socket
*
* returns:
* nothing
*
* changes:
* State of HTTP socket, contents of EEPROM config
*/

static void update_value(unsigned char socketNum)
{

    int val = 0;

    //If value should be changed, check limits
    if (socket_recv_compare(socketNum, "twarn_hi="))
    {

        socket_recv_int(socketNum, &val);

        if (!(val > config.lo_warn && val < config.hi_alarm))
        {

            request = ERROR;
        }

        else
        {

            config.hi_warn = val;
            config_set_modified();

        }

    }

    else if (socket_recv_compare(socketNum, "tcrit_hi="))
    {

        socket_recv_int(socketNum, &val);

        if (!(val > config.hi_warn))
        {

            request = ERROR;
        }

        else
        {

            config.hi_alarm = val;
            config_set_modified();

        }

    }

    else if (socket_recv_compare(socketNum, "twarn_lo="))
    {

        socket_recv_int(socketNum, &val);

        if (!(val > config.lo_alarm && val < config.hi_warn))
        {

            request = ERROR;
        }

        else
        {

            config.lo_warn = val;
            config_set_modified();

        }

    }

    else if (socket_recv_compare(socketNum, "tcrit_lo="))
    {

        socket_recv_int(socketNum, &val);

        if (!(val < config.lo_warn))
        {

            request = ERROR;
        }

        else
        {

            config.lo_alarm = val;
            config_set_modified();

        }

    }

    else
    {

        request = ERROR;

    }

}

/**********************************
* write_vpd(unsigned char socketNum)
*
* This code sends the device VPD through
* the specified socket in JSON format
*
* arguments:
* unsigned char - the specified server socket
*
* returns:
* nothing
*
* changes:
* State of HTTP socket
*/

static void write_vpd(unsigned char socketNum)
{

    socket_writechar(socketNum, '{');
    socket_writequotedstring(socketNum, "vpd");

    socket_writestr(socketNum, ":{");
    socket_writequotedstring(socketNum, "model");

    socket_writechar(socketNum, ':');
    socket_writequotedstring(socketNum, vpd.model);

    socket_writechar(socketNum, ',');
    socket_writequotedstring(socketNum, "manufacturer");

    socket_writechar(socketNum, ':');
    socket_writequotedstring(socketNum, vpd.manufacturer);

    socket_writechar(socketNum, ',');
    socket_writequotedstring(socketNum, "serial_number");

    socket_writechar(socketNum, ':');
    socket_writequotedstring(socketNum, vpd.serial_number);

    socket_writechar(socketNum, ',');
    socket_writequotedstring(socketNum, "manufacture_date");

    socket_writechar(socketNum, ':');
    socket_writedec32(socketNum, vpd.manufacture_date);

    socket_writechar(socketNum, ',');
    socket_writequotedstring(socketNum, "mac_address");

    socket_writechar(socketNum, ':');
    socket_write_macaddress(socketNum, vpd.mac_address);

    socket_writechar(socketNum, ',');
    socket_writequotedstring(socketNum, "country_code");

    socket_writechar(socketNum, ':');
    socket_writequotedstring(socketNum, vpd.country_of_origin);

    socket_writechar(socketNum, '}');

}

/**********************************
* write_temp_values(unsigned char socketNum)
*
* This code sends the temperature limit of the
* device through the specified socket in JSON format
*
* arguments:
* unsigned char - the specified server socket
*
* returns:
* nothing
*
* changes:
* State of HTTP socket
*/

static void write_temp_values(unsigned char socketNum)
{

    socket_writequotedstring(socketNum, "tcrit_hi");
    socket_writechar(socketNum, ':');

    socket_writedec32(socketNum, config.hi_alarm);
    socket_writechar(socketNum, ',');

    socket_writequotedstring(socketNum, "twarn_hi");
    socket_writechar(socketNum, ':');

    socket_writedec32(socketNum, config.hi_warn);
    socket_writechar(socketNum, ',');

    socket_writequotedstring(socketNum, "tcrit_lo");
    socket_writechar(socketNum, ':');

    socket_writedec32(socketNum, config.lo_alarm);
    socket_writechar(socketNum, ',');

    socket_writequotedstring(socketNum, "twarn_lo");
    socket_writechar(socketNum, ':');

    socket_writedec32(socketNum, config.lo_warn);
}

/**********************************
* write_log(unsigned char socketNum)
*
* This code creates event logs through
* the specified socket in JSON format
*
* arguments:
* unsigned char - the specified server socket
*
* returns:
* nothing
*
* changes:
* State of HTTP socket
*/

static void write_log(unsigned char socketNum)
{

    int numEntries = log_get_num_entries();

    socket_writequotedstring(socketNum, "log");
    socket_writestr(socketNum, ":[");

    //Initialization address placeholders
    unsigned long dummy = 0;
    unsigned char dummy2 = 0;

    unsigned long* time = &dummy;
    unsigned char* eventNum = &dummy2;

    for (int i = 0; i < numEntries; i++)
    {

        log_get_record(i, time, eventNum);
        socket_writechar(socketNum, '{');

        socket_writequotedstring(socketNum, "timestamp");
        socket_writechar(socketNum, ':');

        socket_writequotedstring(socketNum, rtc_num2datestr(*time));
        socket_writechar(socketNum, ',');

        socket_writequotedstring(socketNum, "event");
        socket_writechar(socketNum, ':');

        socket_writechar(socketNum, ((*eventNum) + 48));
        socket_writechar(socketNum, '}');

        if (i != (numEntries - 1))
        {

            socket_writechar(socketNum, ',');

        }
    }

    socket_writechar(socketNum, ']');

}
