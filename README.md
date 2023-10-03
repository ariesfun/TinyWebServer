## Tiny WebServer


### 上线地址

项目体验地址：http://47.113.226.70:7878/

测试账号：

用户名：`zhang3` ，密码：`1234`

### 压力测试

**经 `webbench` 压力测试，目前服务器在本地测试，可支持 `7w+` 的并发请求访问**

![压力测试](https://imgbed-funblog.oss-cn-chengdu.aliyuncs.com/img/pressure_test.png)


### 项目简介

**基于 Linux 实现的轻量级 Web 服务器**

- 介绍：实现了一个轻量级 `Web` 服务器，可支持上万用户的并发访问且及时响应，用户可访问服务器上的多种资源。

- **技术栈：非阻塞 socket + epoll + 线程池 + 日志库 + 定时器 + 数据库连接池 + OpenAI-API**

- 使用状态机解析 `HTTP` 请求报文，支持 **`GET`** 和 **`POST`** 请求，用户可进行登录和注册
- 使用高效的 **`epoll` 多路复用**技术结合 **线程池**，来高效处理多客户端连接事件和数据传输事件
- 设计并实现了同步的**日志模块**，并保证了多线程下记录信息的安全写入问题
- 基于红黑树实现了高效的定时器，检测非活跃用户连接，还可协同网络事件及复用系统调用
- 基于生产者消费者模型实现的**数据库连接池**，可动态的创建连接及对非活跃的连接进行回收
- 采用 `LiteWebChat` 前端框架结合 **`gpt-3.5` 模型API**，配置实现了与 `ChatGPT` 聊天服务
- 经 `Webbench` 压力测试，服务器可以支持 **`7W+ QPS`** 并发连接数据交换

### 如何运行

- 先将本项目下载在你的 `Linux` 服务器上
- 更改服务器的配置信息，包括数据库服务器(新建数据库、对应表格)，网站资源的根目录路径
- 确保你指定的服务器端口已经开放
- 编译并运行程序
    ```shell
    mkdir build
    cd build && cmake .. && make
    ./TinyWebser
    ```
- 打开浏览器测试该服务器功能
- 在另外一个窗口中查看日志信息，关注是否有 `Error` 信息打印

