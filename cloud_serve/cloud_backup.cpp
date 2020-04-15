#include "cloud_backup.hpp"
#include <thread>

void compress_test(char *argv[])
{
  //argv[1] = 原文件名称
  //argv[2] = 压缩包名称
  _cloud_sys::CompressUtil::Compress(argv[1],argv[2]);
  std::string file = argv[2];
  file += ".txt";
  _cloud_sys::CompressUtil::UnCompress(argv[2],file.c_str());
}
void test()
{

  _cloud_sys::DataManger data_manage("./test.txt");
  data_manage.InitLoad();
  data_manage.Insert("c.txt","c.txt.gz");
  std::vector<string> list;
  data_manage.GetAllName(&list);
  for(auto i:list)
  {
    cout << i.c_str() << endl;
  }
  
  list.clear();
  data_manage.NonCompressList(&list);
  for(auto i:list)
  {
    cout << i.c_str() << endl;
  }
/*  data_manage.Insert("a.txt","a.txt");

  data_manage.Insert("b.txt","b.txt.gz");
  data_manage.Insert("c.txt","c.txt");
  data_manage.Insert("d.txt","d.txt.gz");
  data_manage.Storage();*/
}
void m_non_compress()
{
  _cloud_sys::NonHotCompress ncom(GZFILE_DIR,BACKUP_DIR);
  ncom.Start();
  return;
}
void thr_http_server()
{
  _cloud_sys::Server srv;
  srv.Start();
  return;
}
int main(int argc,char *argv[])
{
  
  //文件备份路径不存在则创建
  if(boost::filesystem::exists(GZFILE_DIR) == false)
  {
    boost::filesystem::create_directory(GZFILE_DIR);
  }

  //压缩包存放路径不存在则创建
  if(boost::filesystem::exists(BACKUP_DIR) == false)
  {
    boost::filesystem::create_directory(BACKUP_DIR);
  }
  std::thread thr_compress(m_non_compress);   //c++11 中的线程
  std::thread thr_server(thr_http_server);  //网络通信服务端模块启动
  thr_compress.join();  //等待线程退出
  thr_server.join();

  return 0;
}
