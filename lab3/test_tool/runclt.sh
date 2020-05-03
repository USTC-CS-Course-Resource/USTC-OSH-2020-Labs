#!/bin/sh

cw="/home/rabbit/OSH-2020-Labs/lab3/test_tool"
data="$cw/data.txt"
time=50

nc 127.0.0.1 6666 < "$data" > "$cw/out.txt" &

for i in $(seq 1 31)
do
	nc 127.0.0.1 6666 < "$data" > /dev/null &
	sleep 0.01
	echo "$i start"
done

target=33
now=0
total=0

for i in $(seq 1 30)
do
	now=$(($(wc out.txt -l | tr -cd "[0-9]")))
	sleep 3
	total=$(expr $total + $((3)))
	echo "$(wc out.txt), $total s used now."
	if [ $now = $target ]; then
		break
	fi
done

if [ $now != $target ]; then
	echo "[error] $total s used, $now lines, time out"
else
	echo "[ok] $total s used."
fi

ps -a | grep nc | cut -c 1-5 | xargs kill -s 9 > /dev/null

sleep 1
python3 checker.py

