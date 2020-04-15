#include "cloud_client.h"

const string STORE_FILE = "./list.backup";
const string LISTEN_DIR = "./backup/";
const string SERVER_IP = "192.168.35.132";
const uint16_t SERVER_PORT = 9000;
int main()
{
	CloudClient client(LISTEN_DIR, STORE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}