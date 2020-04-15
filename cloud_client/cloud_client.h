#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <sstream>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include "httplib.h"
using namespace std;


class FileUtil
{
public:
	//���ļ��ж�ȡ��������
	static bool Read(const std::string& name, std::string* body)
	{
		//�����Ʒ�ʽ�򿪣�����ᶪʧ�ļ�����
		std::ifstream fs(name, std::ios::binary); //�����ļ���
		if (fs.is_open() == false)
		{
			std::cout << "open file " << name << "failed" << endl;
			return false;
		}

		//boost::filesystem::file_size() ��ȡ�ļ���С
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);   //��body����ռ�����ļ�����
		fs.read(&(*body)[0], fsize);    //body[0] string���еĲ���������  ȡ��string�������ַ����ĵ�����ֽڵ��ַ�
		if (fs.good() == false)           //fs.good()    �ж���һ�β����Ƿ���ȷ
		{
			std::cout << "file" << name << "read data failed!" << endl;
			return false;
		}
		fs.close();
		return true;
	}
	//���ļ���д������
	static bool Write(const std::string& name, const std::string& body)
	{
		//����� --- ofstreamĬ�ϴ��ļ���ʱ������ԭ������
		//��ǰ�����Ǹ���д��
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "open file " << name << "failed" << endl;
			return false;
		}

		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			std::cout << "file " << name << "write data failed!" << endl;
			return false;
		}
		ofs.close();
		return true;
	}
};

class DataManger
{
public:
	DataManger(const std::string& filename)
		:_store_file(filename)
	{}

	bool Insert(const std::string& key, const std::string& val)//����/��������
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}

	bool GetEtag(const std::string& key, std::string* val)//ͨ���ļ�����ȡԭ��etag��Ϣ
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}

		*val = it->second;
		return true;
	}

	bool Storage()//�־û��洢
	{
		std::stringstream tmp;
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it)
		{
			tmp << it->first << " " << it->second << "\r\n";
		}

		FileUtil::Write(_store_file, tmp.str());
		return true;
	}

	bool InitLoad()// ��ʼ������ԭ�е�����
	{
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.���ַ�������\r\n���зָ��
		//boost::splot(vector,src,sep,flag)   sep�ָ��� src�ַ���
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.ÿһ�а��տո���зָ�-ǰ����key�������val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.��key/val��ӵ�_file_list��
			Insert(key, val);
		}

		return true;
	}

private:
	std::string _store_file;//�־û��洢�ļ�����
	std::unordered_map<std::string, std::string> _backup_list;//���ݵ��ļ���Ϣ
};


class CloudClient
{
public:
	CloudClient(const std::string& filename, const std::string& store_file, const std::string& srv_ip, uint16_t srv_port)
		:_listen_dir(filename),
		data_manage(store_file),
		_srv_ip(srv_ip),
		_srv_port(srv_port)
	{}

	bool Start()   //������������
	{
		while (1)
		{
			data_manage.InitLoad();
			std::vector<std::string> list;
			GetBackupFileList(&list); // ��ȡ�����е���Ҫ���ݵ��ļ�����
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;

				cout << pathname << "is need to backup " << endl;
				std::string body;
				FileUtil::Read(pathname, &body);
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//����ļ��ϴ�ʧ����
					cout << pathname << "backup failed" << endl;
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag); //���ݳɹ������//������Ϣ
				//�ļ�����ʧ�� ����¼����Ϣ �´λ�������¼��

				cout << pathname << "backup success" << endl;
			}
			Sleep(1000); // ����1000���������
		}

		return true;
	}

	bool GetBackupFileList(std::vector<std::string>* list)  //��ȡ�ļ������б�
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directories(_listen_dir); // ��Ŀ¼�������򴴽�Ŀ¼
		}
		//1.����Ŀ¼���
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//�����ж�㼶Ŀ¼���ݣ�����Ŀ¼ֱ������
				continue;
			}
			std::string pathname = begin->path().string();
			std::string cur_etag;
			std::string name = begin->path().filename().string();
			GetEtag(pathname, &cur_etag);
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);

			if (cur_etag != old_etag)
			{
				list->push_back(name);  //��ǰetag��ԭ��etag��ͬ�򱸷�
			}
		}
		//2.����ļ���������ǰetag
		//3.��data_manage�б����ԭ��etag
		return true;
	}

	static bool GetEtag(const std::string& pathname, std::string* etag)  //�����ļ���etag��Ϣ
	{

		int64_t fsize = boost::filesystem::file_size(pathname);  //�ļ���С
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;   //����ʱ��
	}

private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir; // ��ص�Ŀ¼����
	DataManger data_manage;
};