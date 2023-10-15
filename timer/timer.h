#ifndef TIMER_H
#define TIMER_H

#include <time.h>
#include <sys/socket.h>

class Timer;
struct ClientData
{
    sockaddr_in address_;
    int sockfd_;
    Timer* timer_;
};

class Timer
{
public:
    Timer():pre_(NULL), next_(NULL){}
public:
    time_t expire_;
    void (*callback_func_)(ClientData*);
    ClientData* userdata_;
    Timer* pre_;
    Timer* next_;

};

//Ascending doubly linked list with head and tail nodes for Timer containing.
class SortTimerList
{
public:
    SortTimerList():head_(NULL),tail_(NULL){}
    ~SortTimerList()
    {
        Timer* temp = head_;
        while(temp)
        {
            head_ = temp->next_;
            delete(temp);
            temp = head_;
        }
    }
    //add timer when timer->expire < head's.
    void AddTimer(Timer* timer)
    {
        if(!timer)
            return ;
        if(!head_)
        {
            head_ = tail_ = timer;
            return;
        }
        if(timer->expire_ < head_->expire_)
        {
            timer->next_ = head_;
            head_->pre_ = timer;
            head_ = timer;
            return ;

        }
        AddTimer(timer, head_);
    }
    void AdjTimer(Timer* timer)
    {
        if(!timer)
            return ;
        Timer* temp = timer->next_;
        if(!temp || (timer->expire_ < temp->expire_))
            return ;
        if(timer == head_)
        {
            head_ = head_->next_;
            head_->pre_ = NULL;
            timer->next_ = NULL;
            AddTimer(timer, head_);
        }
    }
    void DelTimer(Timer* timer)
    {
        if(!timer)
        {
            return ;
        }
        if(timer == head_ && timer == tail_)
        {
            delete timer;
            head_ = NULL;
            tail_ = NULL;
            return ;
        }
        if(head_ == timer)
        {
            head_ = timer->next_;
            head_->pre_ = NULL;
            delete timer;
            return ;
        }
        if(tail_ == timer)
        {
            tail_ = timer->pre_;
            tail_->next_ = NULL;
            delete timer;
            return ;
        }
        timer->pre_->next_ = timer->next_;
        timer->next_->pre_ = timer->pre_;
        delete timer;
    }

    //delete all timer which reach expire time.
    void Tick()
    {
        if(!head_)
        {return ;}
        time_t cur = time(NULL);
        Timer* temp = head_;
        while(temp)
        {
            if(cur < temp->expire_)
                break;
            temp->callback_func_(temp->userdata_);
            head_ = temp->next_;
            if(head_)
            {
                head_->pre_ = NULL;
            }
            delete temp;
            temp = head_;
        }

    }
private:
    //add timer when timer->expire < head's.
    void AddTimer(Timer* timer, Timer* head)
    {
        Timer* pre = head;
        Timer* temp = pre->next_;
        while(temp)
        {
            if(timer->expire_ < temp->expire_)
            {
                pre->next_ = timer;
                timer->pre_ = pre;
                timer->next_ = temp;
                temp->pre_ = timer;
                break;
            }
            pre = temp;
            temp = temp->next_;
        }
        if(!temp)
        {
            pre->next_ = timer;
            timer->pre_ = pre;
            timer->next_ = NULL;
            tail_ = timer;
        }
    }
private:
    Timer* head_;
    Timer* tail_;
};
#endif