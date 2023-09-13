#pragma once

#include <functional>
#include <set>

namespace ariesfun {
namespace timer {

struct TimerNodeBase {
    time_t m_time; // 执行任务的时间点
    int64_t id; // 避免两个事件时同一时间触发的
};

// 定时器节点
struct TimerNode :public TimerNodeBase {
    // 只要符合 void(const TimerNode &node) 这种类型的，都可以当做参数参数传入
    using Callback = std::function<void(const TimerNode &node)>; 
    Callback func; // 回调方法, 要执行的任务
    int repeat; // 重复触发次数， -1无限次进行触发
    time_t m_peroid;
};

// 确保了比较函数只被定义一次, 编译器在编译阶段将函数的代码插入到调用该函数的地方
// 红黑树是通过比较key来维持有序
// 需要实现一个比较函数，越放在左边的越先执行，从而解决线程安全的问题
inline bool operator < (const TimerNodeBase &l, const TimerNodeBase &r)
{
    if(l.m_time < r.m_time) {
        return true; // 放左边
    }
    else if(l.m_time > r.m_time) {
        return false; // 放右边
    }
    return l.id < r.id; // 相等，后插入的任务放右边
}

class Timer {
public:
    static time_t GetTick();    // 获取当前系统时间戳 (ms)
    static int64_t GenID();     // 得到定时器的id;

    // 添加定时器
    // 第3个参数是多久过期后，要调用的方法（传一个函数对象），需要用 (ms+func) 构造一个定时器节点，来描述具体的定时任务
    TimerNodeBase AddTimer(time_t ms, int repeat, TimerNode::Callback func);

    // 删除定时器
    bool DelTimer(TimerNodeBase &node);

    // 判断定时任务是否过期，来执行定时任务
    bool CheckTimer();

    // 要找到最近触发的定时任务离当前的时间，休息时间
    time_t TimeToSleep(); 

private:
    static int64_t m_id;
    std::set<TimerNode, std::less<>> m_map; // key是时间节点，value是比较器
};

}}
