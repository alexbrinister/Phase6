/**
* \file Main.cpp
* \details Main program
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

/* Standard C++ Library headers */
#include <iostream>
#include <exception>
#include <algorithm>
#include <string>

/* Standard C Library Headers */
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <unistd.h>
#include <getopt.h>

#include "UdpServerTCP.hpp"

/// Usage print out
// TODO : Use C++ string formatting functionality on this to make it nicer.
void Usage(char** argv)
{
    std::cout << "USAGE:\t\t" << argv[0] << std::endl << std::endl;

    std::cout << "\t\t--client      |   --server" << std::endl;
    std::cout << "\t\t--myport      |   -m <port number>" << std::endl;
    std::cout << "\t\t--dstport     |   -d <port number>" << std::endl;
    std::cout << "\t\t(--serverAddr |   -s <hostname/IP>)" << std::endl;
    std::cout << "\t\t--infile      |   -i <file path>" << std::endl;
    std::cout << "\t\t--outfile     |   -o <file path>" << std::endl;
    std::cout << "\t\t--derror      |   -w <percent (without %)>" << std::endl;
    std::cout << "\t\t--aerror      |   -x <percent (without %)>" << std::endl;
    std::cout << "\t\t--dloss       |   -w <percent (without %)>" << std::endl;
    std::cout << "\t\t--aloss       |   -x <percent (without %)>" << std::endl;
}

/// Main program function
int main(int argc, char** argv)
{
    std::string serverAddr;

    std::string inFile;
    std::string outFile;

    unsigned int myPort = 0;
    unsigned int dstPort = 0;

    unsigned int dataErrorPercent = 0;
    unsigned int ackErrorPercent = 0;

    unsigned int dataLossPercent = 0;
    unsigned int ackLossPercent = 0;

    // Represents mode; 0 for server, 1 for client.
    // Set by command line
    int modeFlag = 0;

    int inputOpt = 0;
    int prevOptIdx = 0;

    static struct option longOpts[] =
    {
        {"help", no_argument, 0, 'h'},
        {"serverAddr", required_argument, nullptr, 's'},
        {"myport", required_argument, nullptr, 'm'},
        {"dstport", required_argument, nullptr, 'd'},
        {"derror", required_argument, nullptr, 'w'},
        {"aerror", required_argument, nullptr, 'x'},
        {"dloss", required_argument, nullptr, 'y'},
        {"aloss", required_argument, nullptr, 'z'},
        {"client", no_argument, &modeFlag, 1},
        {"server", no_argument, &modeFlag, 0},
        {"infile", required_argument, nullptr, 'i'},
        {"outfile", required_argument, nullptr, 'o'},
        {0, 0, 0, 0}
    };

    try
    {
        // Start getopt
        for (;;)
        {
            prevOptIdx = optind;

            inputOpt = getopt_long(argc,
                    argv,
                    "h:s:m:d:w:x:y:z:i:o:",
                    longOpts,
                    nullptr);

            // End of options
            if (inputOpt == -1)
            {
                // If the option index is still at the beginning, it means no
                // options were entered.
                if (optind == 0)
                {
                    Usage(argv);
                    exit(1);
                }

                break;
            }

            // This solution for missing args was taken from
            // https://stackoverflow.com/a/2219710
            if( optind == prevOptIdx + 2 && *optarg == '-' )
            {
                inputOpt = ':';
                --optind;
            }

            // Go through the options and figure out which ones we got
            switch (inputOpt)
            {
                // In this case, the cmdline option has no argument. So we just
                // continue through the options.
                case 0:
                    break;

                    // Help has been invoked, print usage
                case 'h':
                    Usage(argv);
                    return 0;

                    // Server string option
                case 's':
                    serverAddr = std::string(optarg);
                    break;

                    // Local host machine port
                    // TODO: The method used to detect an empty argument needs
                    // to become a function.
                case 'm':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify the local port!";
                            std::cout << std::endl;
                            exit(1);
                        }

                        myPort = std::stoul(optString);
                        break;
                    }

                    // Destination host machine port
                case 'd':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify the local port!";
                            std::cout << std::endl;
                            exit(1);
                        }

                        dstPort = std::stoul(optString);
                        break;
                    }

                    // Input file name
                case 'i':
                    inFile = std::string(optarg);
                    break;

                    // Output file name
                case 'o':
                    outFile = std::string(optarg);
                    break;

                    // Data bit error percent
                case 'w':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify a value for ";
                            std::cout << "data bit error percent!" << std::endl;
                            exit(1);
                        }

                        dataErrorPercent = std::stoul(optString);
                        break;
                    }

                    // ACK bit error percent
                case 'x':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify a value for ";
                            std::cout << "ACK bit error percent!" << std::endl;
                            exit(1);
                        }

                        ackErrorPercent = std::stoul(optString);
                        break;
                    }

                    // Data packet loss percent
                case 'y':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify a value for data ";
                            std::cout << "packet loss percent!" << std::endl;
                            exit(1);
                        }

                        dataLossPercent = std::stoul(optString);
                        break;
                    }

                    // ACK packet loss percent
                case 'z':
                    {
                        std::string optString = std::string(optarg);
                        if(optString.empty())
                        {
                            std::cout << "You must specify a value for ack ";
                            std::cout << "packet loss percent!" << std::endl;
                            exit(1);
                        }

                        ackLossPercent = std::stoul(optString);
                        break;
                    }

                case ':':
                    std::cout << "Missing option in argument '";
                    std::cout << argv[optind] << "'";
                    std::cout << std::endl;
                    Usage(argv);
                    exit(1);
                    break;

                case '?':
                    Usage(argv);
                    exit(1);
                default:
                    break;
            }
        }

        /**********************************************************************
        * In this section, we check if the arguments were specified to begin
        * with.  If they weren't, then we need to either error out or set some
        * default values. In most cases, we error out because we initialize
        * all of our values when we begin the application
        *********************************************************************/
        if (myPort == 0)
        {
            std::cout << "You must specify the local port!" << std::endl;
            Usage(argv);
            exit(1);
        }

        // Input file must be specified
        if(inFile.empty())
        {
            std::cout << "You must specify an input file!" << std::endl;
            Usage(argv);

            exit(1);
        }

        // Output file must be specified
        if(outFile.empty())
        {
            std::cout << "You must specify an output file!" << std::endl;
            Usage(argv);

            exit(1);
        }

        if(modeFlag == 1)
        {
            if (dstPort == 0)
            {
                std::cout << "You must specify the local port!" << std::endl;
                Usage(argv);
                exit(1);
            }

            // If client is specified, host cannot be empty
            if(serverAddr.empty())
            {
                std::cout << "You must specify a destination server in ";
                std::cout << "client mode" << std::endl;

                Usage(argv);

                exit(1);
            }
        }


        // Now, we run either the server or client
        if (modeFlag == 0) /* Server mode... */
        {
            socksahoy::UdpServerTCP server(myPort);

            server.Listen(outFile,
                    inFile,
                    ErrorPercent,
                    LossPercent,
                    false);
        }

        else if (modeFlag == 1) /* Client mode... */
        {
            socksahoy::UdpServerTCP server(myPort);

            server.Send(dstPort,
                    serverAddr,
                    outFile,
                    inFile,
                    ErrorPercent,
                    LossPercent,
                    false);
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        exit(1);
    }

    return 0;
}

// vim: set expandtab ts=4 sw=4: