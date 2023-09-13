#include <iostream>
#include <sys/epoll.h>
#include <chrono>
#include <memory>

#include "timer.h"
using namespace ariesfun::timer;

int64_t Timer::m_id = 0;

time_t Timer::GetTick() 
{
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()); // 获得当前时间的时间点
    auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()); // 获取时间间隔，表示从系统纪元1971到当前时间点的时间段
    return gap.count(); // 获得具体毫秒数值
}

int64_t Timer::GenID() // 得到定时器的id
{
    return m_id++;
}

// 添加定时器
// 第二个参数是多久过期后，要调用的方法（传一个函数对象）
// 需要用 (ms+func) 构造一个定时器节点，来描述具体的定时任务
TimerNodeBase Timer::AddTimer(time_t ms, int repeat, TimerNode::Callback func) 
{
    TimerNode t_node;
    t_node.m_time = GetTick() + ms; // 获得任务触发的时间点
    t_node.func = func; // 具体的定时任务
    t_node.id = GenID(); // 标识当前定时器节点
    t_node.repeat = repeat; // 记录重复触发的次数
    t_node.m_peroid = ms; // 每次执行的周期

    // 需要将TimerNode加到某个数据结构中去
    m_map.insert(t_node);
    return static_cast<TimerNodeBase>(t_node); // 便于进行删除
}

// 删除定时器
bool Timer::DelTimer(TimerNodeBase &node)
{
    // 从某个数据结构中删除定时器
    auto it = m_map.find(node);
    if(it != m_map.end()) {
        m_map.erase(it);
        return true;
    }
    return false;
}

// 判断定时任务是否过期，来执行定时任务
bool Timer::CheckTimer()
{
    // 先要找到某个数据结构最小的时间节点t  
    // 若 now >= t : 节点已经被触发需要去执行
    auto it = m_map.begin();
    if(it != m_map.end() && it->m_time <= GetTick()) { // 有元素，节点已经触发需要执行任务
        TimerNode &node = const_cast<TimerNode &>(*it); // 去除const
        node.func(node);
        // it->func(*it); // 具体的接口就是it本身，参数引用值本身，需要去执行
        if(node.repeat != -1) { // 不是无限触发
            (node.repeat)--;
            if(node.repeat == 0) { 
                m_map.erase(it); // 事件已经处理完成
            }
            else {
                node.m_time += node.m_peroid; // 更新下一次触发的时间
            }
        }
        else {
            node.m_time += node.m_peroid;
        }
        return true;
    }
    return false;
}

// 要找到最近触发的定时任务离当前的时间
time_t Timer::TimeToSleep()
{
    auto it = m_map.begin();
    if(it == m_map.end()) { // 无任何时间节点
        return -1; // 返回-1,永久阻塞
    }
    time_t dis = (it->m_time) - GetTick();
    return dis > 0 ? dis : 0;  // dis<=0 没有事件会立即返回，就可以直接处理网络任务了
}
