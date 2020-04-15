#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <zlib.h>
#include "httplib.h"
#include <pthread.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
using namespace std;
#define NONHOT_TIME 10  //最后一次访问时间在10秒内
#define INTERVAL_TIME 30 //非热点检测30秒一次
#define BACKUP_DIR "./backup/"   //文件备份路径
#define GZFILE_DIR "./gzfile/"   //压缩包存放路径
#define DATA_FILE "./list.backup"  //数据管理模块的数据备份文件名称

namespace _cloud_sys
{
  class FileUtil
  {
    public:
      //从文件中读取所有内容
      static bool Read(const std::string &name,std::string *body)
      {
          //二进制方式打开，否则会丢失文件内容
        std::ifstream fs(name,std::ios::binary); //输入文件流
        if(fs.is_open() == false)
        {
          std::cout << "open file " << name << "failed" <<endl;
          return false;
        }
        
        //boost::filesystem::file_size() 获取文件大小
        int64_t fsize = boost::filesystem::file_size(name);
        body->resize(fsize);   //给body申请空间接受文件数据
        fs.read(&(*body)[0],fsize);    //body[0] string类中的操作符重载  取出string对象中字符串的第零个字节的字符
        if(fs.good() == false)           //fs.good()    判断上一次操作是否正确
        {
          std::cout << "file" << name << "read data failed!" <<endl;
          return false;
        }
        fs.close();
        return true;
      }
      //像文件中写入数据
      static bool Write(const std::string &name,const std::string &body)
      {
        //输出流 --- ofstream默认打开文件的时候会清空原有内容
        //当前策略是覆盖写入
        std::ofstream ofs(name,std::ios::binary);
        if(ofs.is_open() == false)
        {
          std::cout << "open file " << name << "failed" << endl;
          return false;
        }

        ofs.write(&body[0],body.size());
        if(ofs.good() == false)
        {
          std::cout << "file " << name << "write data failed!" << endl;
          return false;
        }
        ofs.close();
        return true;
      }
  };

  class CompressUtil
  {
    public:
      //文件压缩-原文件名称-压缩包名称
      static bool Compress(const std::string &src,const std::string &dst)
      {
        std::string body;
        FileUtil::Read(src,&body);

        gzFile gf = gzopen(dst.c_str(),"wb"); //打开压缩包
        if(gf == nullptr)
        {
          std::cout << "open file " << dst << "failed!" << endl;
          return false;
        }
        int wlen = 0;
        while(wlen < body.size())  //防止body中的数据没有压缩完
        {
          //若一次没有将全部数据压缩，则从未压缩的地方开始继续压缩
          int ret = gzwrite(gf,&body[wlen],body.size() - wlen);
          if(ret == 0)
          {
            std::cout << "file " << dst << "write compress data failed!" <<endl;
            return false;
          }
          wlen = wlen + ret;
        }
        gzclose(gf);
        return true;
      }
      //文件解压缩-压缩包名称-原文件名称
      static bool UnCompress(const std::string &src,const std::string &dst)
      {
        std::ofstream ofs(dst,std::ios::binary);
        if(ofs.is_open() == false)
        {
          std::cout << "open file " << dst << "failed!" <<endl;
          return false;
        }

        gzFile gf = gzopen(src.c_str(),"rb");
        if(gf == NULL)
        {
          std::cout << "open file " << src << " failed!" <<endl;
          ofs.close();
          return false;
        }
        
        char tmp[4096] = {0};
        int ret;
        //gzread(句柄，缓冲区，缓冲区大小)
        //返回实际读取到的解压后的数据大小
        while((ret = gzread(gf,tmp,4096)) > 0)
        {
          ofs.write(tmp,ret);
        }
        ofs.close();
        gzclose(gf);

        return true;
      }

  };
  
  class DataManger
  {
    public:
      DataManger(const std::string &path)
        :_back_file(path)
      {
        pthread_rwlock_init(&_rwlock,nullptr);
      }

      ~DataManger()
      {
        pthread_rwlock_destroy(&_rwlock);
      }

      //判断文件是否存在
      bool Exists(const std::string &name)
      {
        pthread_rwlock_rdlock(&_rwlock);
        auto cur = _file_list.find(name);
        if(cur == _file_list.end())
        {
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        return true;
      }

      //判断文件是否已经压缩
      bool IsCompress(const std::string &name)
      {
        //管理的数据内容：原文件名称：压缩后的文件名称
        //判断原文件名称和压缩后的名称是否一致
        pthread_rwlock_rdlock(&_rwlock);
        auto cur = _file_list.find(name);
        if(cur == _file_list.end())
        {
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }

        if(cur->first == cur->second)
        {
          pthread_rwlock_unlock(&_rwlock);
          return false;      //名称一样表示没有压缩
        }

        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      //获取未压缩文件列表
      bool NonCompressList(std::vector<std::string> *list) 
      {
        //遍历找出没有压缩的文件添加到list中
        pthread_rwlock_rdlock(&_rwlock);
        auto cur = _file_list.begin();
        for(;cur != _file_list.end();++cur)
        {
          if(cur->first == cur->second)
          {
            list->push_back(cur->first);
          }
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }
      //插入/更新数据
      bool Insert(const std::string &src,const std::string &dst)
      {
        pthread_rwlock_wrlock(&_rwlock);  //写锁
        _file_list[src] = dst;
        pthread_rwlock_unlock(&_rwlock);
        Storage(); //跟新之后重新备份
        return true;
      }
      //获取所有文件名称
      bool GetAllName(std::vector<std::string> *list)
      {
        pthread_rwlock_rdlock(&_rwlock);
        auto cur = _file_list.begin();
        for(;cur != _file_list.end();++cur)
        {
          //获取原文件名称
          list->push_back(cur->first); 
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool GetGzName(const std::string &src,std::string *dst)
      {
        auto it = _file_list.find(src);
        if(it == _file_list.end())
        {
          return false;
        }
        *dst = it->second;
        return true;
      }
      //数据改变后的持久化存储
      bool Storage()
      {
        //将_file_list中的数据进行持久化存储
        //数据对象进行持久化存储---序列化
        //src dst\r\n
        std::stringstream tmp;   //实例化一个string流对象
        pthread_rwlock_rdlock(&_rwlock);   //没有修改数据因此是读锁
        auto it = _file_list.begin();
        for(;it != _file_list.end();++it)
        {
          tmp << it->first << " " << it->second << "\r\n";
        }
        pthread_rwlock_unlock(&_rwlock);
        //将数据备份
        FileUtil::Write(_back_file,tmp.str());  //tmp.str()  获取到string流里的string对象
        return true;
      }
      //启动时初始化加载原有数据
      //格式 ：filename gzfilename\r\nfilename gzfilename\r\n...
      bool InitLoad()
      {
        //1.从备份文件中读取数据
        std::string body;
        if(FileUtil::Read(_back_file,&body) == false)
        {
          return false;
        }
        //2.将字符串按照\r\n进行分割处理
        //boost::splot(vector,src,sep,flag)   sep分隔符 src字符串
        std::vector<std::string> list;
        boost::split(list,body,boost::is_any_of("\r\n"),boost::token_compress_off);
        //3.每一行按照空格进行分割-前边是key，后边是val
        for(auto i:list)
        {
          size_t pos = i.find(" ");
          if(pos == std::string::npos)
          {
            continue;
          }
          std::string key = i.substr(0,pos);
          std::string val = i.substr(pos+1);
          //4.将key/val添加到_file_list中
          Insert(key,val);
        }
        
        return true;
      }
    private:
      std::string _back_file;  //持久化数据存储文件名称
      std::unordered_map<std::string,std::string> _file_list; //数据管理容器
      pthread_rwlock_t _rwlock;
  };

  //文件数据传上来，会备份到服务器的文件数，数据管理都是管理的文件名称
  
  //热点文件压缩
  DataManger data_manage(DATA_FILE);
  class NonHotCompress 
  {
      public:
        NonHotCompress(const std::string gz_dir,const std::string bu_dir)
          :_gz_dir(gz_dir),
           _bu_dir(bu_dir)
        {}
        ~NonHotCompress()
        {}
        bool Start()  //总体向外提供的功能接口，开始压缩模块
        {
          while(1)
          {
          //1.获取所有未压缩的文件列表
              std::vector<std::string> list;
              data_manage.NonCompressList(&list);
          // 2.逐个判断这个文件是否是热点文件 
              for(int i = 0;i < list.size();i++)
              {
                bool ret = FileIsHot(list[i]);
                if(ret == false)
                {
                  cout << "non hot file " << list[i] << endl;
                  std::string s_filename = list[i]; //纯源文件名称
                  std::string d_filename = list[i] + ".gz";
                  std::string src_name = _bu_dir + s_filename;  //原文件路径名称
                  std::string dst_name = _gz_dir + d_filename;
                  //3.如果是非热点文件则压缩这个文件，删除原文件
                  if(CompressUtil::Compress(src_name,dst_name) == true)
                  {
                    data_manage.Insert(s_filename,d_filename); // 跟新数据信息
                    unlink(src_name.c_str());
                  }
                }
              }
              //4.休眠一会
              sleep(INTERVAL_TIME);
          }

          return true;
        }
      private:
        //判断一个文件是否是一个热点文件
        bool FileIsHot(const std::string &name)
        {
          //非热点文件 == 当前时间 - 最后一次访问时间 > 0
          time_t cur_t = time(NULL);
          struct stat st;  //stat 打印出一个信息节点的内容
          if(stat(name.c_str(),&st) < 0)
          {
            std::cout << "get file " << name << " stat failed!\n";
            return false;
          }

          if((cur_t - st.st_atime) > NONHOT_TIME)
          {
            return false; // 非热点返回false
          }

          return true; // nonhot_time 以内都是热点文件
        }
        std::string _bu_dir;  //
        std::string _gz_dir;  //压缩后的文件存储路径
  };

  class Server
  {
      public:
        Server()
        {}
        ~Server()
        {}
        bool Start()//启动网络通信模块接口
        {
          _server.Put("/(.*)",Upload);
          _server.Get("/list",List);
          _server.Get("/download/(.*)",Download);  //为了避免有文件名叫list与list请求混淆
          
          _server.listen("0.0.0.0",9000);
          return true;
        }
      private:
        //文件上传处理回调函数
        static void Upload(const httplib::Request &req,httplib::Response &rsp)
        {
          //req.method-解析出的请求方法
          //req.path-解析出的请求的资源路径
          //req.headers-这是一个头部信息的键值对
          //req.body - 存放请求数据的正文
          std::string filename = req.matches[1];  //纯文件名称
          std::string pathname = BACKUP_DIR + filename;  //组织文件路径名文件备份在指定路径
          FileUtil::Write(pathname,req.body);  //向文件写入数据，文件不存在会创建
          // rsp.set_content("upload",6,"text/html");
          //_content(正文数据，正文数据长度，正文类型-Content-Type);
          data_manage.Insert(filename,filename);
          rsp.status = 200;
          return;
        }
        //文件列表处理回调函数
        static void List(const httplib::Request &req,httplib::Response &rsp)
        {
          std::vector<std::string> list;
          data_manage.GetAllName(&list);
          std::stringstream tmp;
          tmp << "<html><body><hr />";
          for(int i = 0;i<list.size();i++)
          {
            tmp << "<a href='/download/" << list[i] << "'>" << list[i] << "</a>";
             //tmp <<"<a href='/download/a.txt'>a.txt</a>"
            tmp << "<hr />";
          }
          tmp << "<hr /></body></html>";
          //http响应格式：首行（协议版本 状态码 描述）
          rsp.set_content(tmp.str().c_str(),tmp.str().size(),"text/html");
          rsp.status = 200;

          return;
        }
        //var文件下载处理回调函数
        static void Download(const httplib::Request &req,httplib::Response &rsp)
        {
          //1.从数据模块中判断文件是否存在
          std::string filename = req.matches[1];   //前边路由注册捕捉的（。*）
          if(data_manage.Exists(filename) == false)
          {
            rsp.status = 404;  //文件不存在则page not found
            return;
          }
          //2.判断文件是否已经压缩，压缩了则先要解压缩 ，然后在读取文件数据
          std::string pathname = BACKUP_DIR + filename;  //filename 原文件备份路径
          if(data_manage.IsCompress(filename) == true)
          {
            //文件被压缩，先将文件解压缩
            //pathname 原文件路径
            std::string gzfile;
            data_manage.GetGzName(filename,&gzfile);
            std::string gzpathname = GZFILE_DIR + gzfile; //组织一个压缩包的路径
            CompressUtil::UnCompress(gzpathname,pathname);//将压缩包解压
            unlink(gzpathname.c_str()); //删除压缩包
            data_manage.Insert(filename,filename); //跟新数据
          }
            //从文件中读取数据,响应给客户端
          FileUtil::Read(pathname,&rsp.body);//直接将文件数据读取到rsp的body中
          rsp.set_header("Content-Type","application/octet-stream");  //二进制流文件下载
          rsp.status = 200;
          return;
        }

        std::string _file_dir;//文件上传备份路径
        httplib::Server _server;
        
  };
}
