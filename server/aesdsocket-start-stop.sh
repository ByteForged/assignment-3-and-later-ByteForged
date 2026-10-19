#!/bin/sh

if [ $# -ne 1 ];
then
    echo "Invalid number of arguements:"
    echo "Ex: $0 <start|stop>"
    exit 1
fi

case "$1" in
    start)
        
        # PATH=$PATH:$(pwd)
        # echo "$PATH"
        echo "Starting aesdsocket server daemon"
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        if [ $? -eq 0 ];
        then
            echo "Daemon started"
        else
            echo "Failed to start daemon"
            exit 1
        fi 
        ;;
    stop)
        echo "Stopping aesdsocket server daemon"
        start-stop-daemon -K -n aesdsocket
        if [ $? -eq 0 ];
        then
            echo "Daemon stopped"
        else
            echo "Failed to stop daemon"
            exit 1
        fi 
        ;;
    *)
        echo "Invalid arguement"
        exit 1
        ;;
esac