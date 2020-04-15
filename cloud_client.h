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
	//从文件中读取所有内容
	static bool Read(const std::string& name, std::string* body)
	{
		//二进制方式打开，否则会丢失文件内容
		std::ifstream fs(name, std::ios::binary); //输入文件流
		if (fs.is_open() == false)
		{
			std::cout << "open file " << name << "failed" << endl;
			return false;
		}

		//boost::filesystem::file_size() 获取文件大小
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);   //给body申请空间接受文件数据
		fs.read(&(*body)[0], fsize);    //body[0] string类中的操作符重载  取出string对象中字符串的第零个字节的字符
		if (fs.good() == false)           //fs.good()    判断上一次操作是否正确
		{
			std::cout << "file" << name << "read data failed!" << endl;
			return false;
		}
		fs.close();
		return true;
	}
	//像文件中写入数据
	static bool Write(const std::string& name, const std::string& body)
	{
		//输出流 --- ofstream默认打开文件的时候会清空原有内容
		//当前策略是覆盖写入
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

	bool Insert(const std::string& key, const std::string& val)//插入/跟新数据
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}

	bool GetEtag(const std::string& key, std::string* val)//通过文件名获取原有etag信息
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}

		*val = it->second;
		return true;
	}

	bool Storage()//持久化存储
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

	bool InitLoad()// 初始化加载原有的数据
	{
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.将字符串按照\r\n进行分割处理
		//boost::splot(vector,src,sep,flag)   sep分隔符 src字符串
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.每一行按照空格进行分割-前边是key，后边是val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.将key/val添加到_file_list中
			Insert(key, val);
		}

		return true;
	}

private:
	std::string _store_file;//持久化存储文件名称
	std::unordered_map<std::string, std::string> _backup_list;//备份的文件信息
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

	bool Start()   //完成整体的流程
	{
		while (1)
		{
			data_manage.InitLoad();
			std::vector<std::string> list;
			GetBackupFileList(&list); // 获取到所有的需要备份的文件名称
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
					//这个文件上传失败了
					cout << pathname << "backup failed" << endl;
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag); //备份成功则插入//更新信息
				//文件插入失败 不会录入信息 下次还会重新录入

				cout << pathname << "backup success" << endl;
			}
			Sleep(1000); // 休眠1000毫秒后重启
		}

		return true;
	}

	bool GetBackupFileList(std::vector<std::string>* list)  //获取文件备份列表
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directories(_listen_dir); // 若目录不存在则创建目录
		}
		//1.进行目录监控
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//不进行多层级目录备份，遇到目录直接跳过
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
				list->push_back(name);  //当前etag与原有etag不同则备份
			}
		}
		//2.逐个文件计算自身当前etag
		//3.与data_manage中保存的原有etag
		return true;
	}

	static bool GetEtag(const std::string& pathname, std::string* etag)  //计算文件的etag信息
	{

		int64_t fsize = boost::filesystem::file_size(pathname);  //文件大小
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;   //访问时间
	}

private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir; // 监控的目录名称
	DataManger data_manage;
};