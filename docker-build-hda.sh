#!/bin/sh

CONTAINER_ID=$(docker create --privileged -w "/" -u root -it ubuntu)
docker start $CONTAINER_ID
docker cp sysroot $CONTAINER_ID:/sysroot
docker cp gen-hda.sh $CONTAINER_ID:/
docker cp update-hda.sh $CONTAINER_ID:/
docker exec $CONTAINER_ID "/gen-hda.sh"
docker exec -u root $CONTAINER_ID "/update-hda.sh"
docker cp $CONTAINER_ID:/hda.img .
docker stop $CONTAINER_ID
docker container rm $CONTAINER_ID
