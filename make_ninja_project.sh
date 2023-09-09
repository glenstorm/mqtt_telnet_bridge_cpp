rm -r build
mkdir build
cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ../
ninja -v
#cgdb ./mqtt_telnet_bridge
./mqtt_telnet_bridge
