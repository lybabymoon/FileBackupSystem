cloud_backup:cloud_backup.cpp
	g++ -std=c++0x $^ -o $@ -lz -lpthread -lboost_filesystem -lboost_system
