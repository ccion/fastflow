/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

/*! 
 *  \file gt.hpp
 *  \ingroup building_blocks
 *
 *  \brief Farm Collector (it is not a ff_node)
 *
 * It Contains the \p ff_gatherer class and methods which are used to model the \a
 *  Collector node, which is optionally used to gather tasks coming from
 *  workers.
 *
 * \todo Documentation to be rewritten. To be substituted with ff_minode?
 */

/* ***************************************************************************
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License version 3 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 ****************************************************************************
 */

#ifndef FF_GT_HPP
#define FF_GT_HPP

#include <iosfwd>
#include <deque>
#include <ff/svector.hpp>
#include <ff/utils.hpp>
#include <ff/node.hpp>

namespace ff {


/*!
 *  \class ff_gatherer
 *  \ingroup building_blocks
 *
 *  \brief A class representing the \a Collector node in a \a Farm skeleton.
 *
 *  This class models the \p gatherer, which wraps all the methods and
 *  structures used by the \a Collector node in a \p Farm skeleton. The \p farm
 *  can be seen as a three-stages \p pipeline, the stages being a \p
 *  ff_loadbalancer called \a emitter, a pool of \p ff_node called \a workers
 *  and - optionally - a \p ff_gatherer called \a collector. The \a Collector
 *  node can be used to gather the results coming from the computations
 *  executed by the pool of \a workers. The \a collector can also be
 *  connected to the \a emitter node via a feedback channel, in order to create
 *  a \p farm-with-feedback skeleton.
 *
 *  This class is defined in \ref gt.hpp
 *
 */

class ff_gatherer: public ff_thread {

    template <typename T1, typename T2>  friend class ff_farm;
    friend class ff_ofarm;
    friend class ff_pipeline;
    friend class ff_minode;
public:
    enum {TICKS2WAIT=5000};

protected:

#if defined(BLOCKING_MODE)
    inline void get_done(int id) {
        pthread_mutex_lock(&workers[id]->get_prod_m());
        if (atomic_long_read(&(workers[id]->get_prod_counter())) >= workers[id]->get_out_buffer()->buffersize()) {
            pthread_cond_signal(&workers[id]->get_prod_c());
        }
        atomic_long_dec(&(workers[id]->get_prod_counter()));
        pthread_mutex_unlock(&workers[id]->get_prod_m());
        atomic_long_dec(&cons_counter);
    }

    inline void push_done() {
        pthread_mutex_lock(p_cons_m);
        if (atomic_long_read(p_cons_counter) == 0) {
            pthread_cond_signal(p_cons_c);
        }
        atomic_long_inc(p_cons_counter);
        pthread_mutex_unlock(p_cons_m);
        atomic_long_inc(&prod_counter);
    }

    inline bool init_input_blocking(pthread_mutex_t *&m,
                                    pthread_cond_t  *&c,
                                    atomic_long_t   *&counter) {
        if (atomic_long_read(&cons_counter) == (unsigned long)-1) { 
            if (pthread_mutex_init(&cons_m, NULL) != 0) return false;
            if (pthread_cond_init(&cons_c, NULL) != 0) {
                pthread_mutex_destroy(&cons_m);
                return false;
            }
            atomic_long_set(&cons_counter,0);
        } 
        m = &cons_m,  c = &cons_c, counter = &cons_counter;
        return true;
    }
    inline void set_input_blocking(pthread_mutex_t *&m,
                                   pthread_cond_t  *&c,
                                   atomic_long_t   *&counter) {
        assert(1==0);
    }    

    // producer
    inline bool init_output_blocking(pthread_mutex_t *&m,
                                     pthread_cond_t  *&c,
                                     atomic_long_t   *&counter) {
        if (atomic_long_read(&prod_counter) == (unsigned long)-1) {
            if (pthread_mutex_init(&prod_m, NULL) != 0) return false;
            if (pthread_cond_init(&prod_c, NULL) != 0) {
                pthread_mutex_destroy(&prod_m);
                return false;
            }
            atomic_long_set(&prod_counter,0);
        } 
        m = &prod_m, c = &prod_c, counter = &prod_counter;
        return true;
    }
    inline void set_output_blocking(pthread_mutex_t *&m,
                                    pthread_cond_t  *&c,
                                    atomic_long_t  *&counter) {
        p_cons_m = m, p_cons_c = c, p_cons_counter = counter;
    }

    virtual inline pthread_mutex_t &get_prod_m()       { return prod_m;}
    virtual inline pthread_cond_t  &get_prod_c()       { return prod_c;}
    virtual inline atomic_long_t   &get_prod_counter() { return prod_counter;}


#endif

    /**
     * \brief Selects a worker.
     * 
     * It gets the next worker using the Round Robin policy. The selected
     * worker has to be alive (and kicking).
     *
     * \return The next worker to be selected.
     *
     */
    virtual inline ssize_t selectworker() { 
        do 
            nextr = (nextr+1) % running;
        while(offline[nextr]);
        return nextr;
    }

    /**
     * \brief Notifies the EOS
     *
     * It is a virtual function and is used to notify EOS
     */
    virtual inline void notifyeos(int id) {}

    /**
     * \brief Gets the number of tentatives.
     *
     * The number of tentative before wasting some times and than retry 
     */
    virtual inline size_t ntentative() { return getnworkers();}

    /**
     * \brief Loses the time out.
     *
     * It is a virutal function which defines the number of ticks to be waited.
     *
     */
    virtual inline void losetime_out(unsigned long ticks=TICKS2WAIT) { 
        FFTRACE(lostpushticks+=ticks;++pushwait);
#if defined(SPIN_USE_PAUSE)
        const long n = (long)ticks/2000;
        for(int i=0;i<=n;++i) PAUSE();
#else
        ticks_wait(ticks);
#endif /* SPIN_USE_PAUSE */
    }
    
    /**
     * \brief Loses the time in 
     *
     * It is a virutal function which defines the number of ticks to be waited.
     *
     */
    virtual inline void losetime_in(unsigned long ticks=TICKS2WAIT) { 
        FFTRACE(lostpopticks+=ticks;++popwait);
#if defined(SPIN_USE_PAUSE)
        const long n = (long)ticks/2000;
        for(int i=0;i<=n;++i) PAUSE();
#else
        ticks_wait(ticks);
#endif /* SPIN_USE_PAUSE */
    }    

    /**
     * \brief It gathers the tasks.
     *
     * It keeps selecting the worker. If a worker has task, then the worker is
     * returned. Otherwise a tick is wasted and then keep looking for the
     * worker with the task.
     *
     * \return It returns the workers with a taks if successful. Otherwise -1
     * is returned.
     */
    virtual ssize_t gather_task(void ** task) {
        unsigned int cnt;
        do {
            cnt=0;
            do {
                nextr = selectworker();
                //assert(offline[nextr]==false);
                if (workers[nextr]->get(task)) {
#if defined(BLOCKING_MODE)
                    get_done(nextr);
#endif
                    return nextr;
                }
                else if (++cnt == ntentative()) break;
            } while(1);
#if !defined(BLOCKING_MODE)
            losetime_in();
#else
            pthread_mutex_lock(&cons_m);
            while(atomic_long_read(&cons_counter) == 0) {
                //fprintf(stderr, "Collector goes to sleep\n");
                pthread_cond_wait(&cons_c, &cons_m);
            }
            pthread_mutex_unlock(&cons_m);
#endif
        } while(1);

        // not reached
        return -1;
    }

    /**
     * \brief Pushes the task in the tasks queue.
     *
     * It pushes the tasks in a queue. 
     */
    inline bool push(void * task, unsigned long retry=((unsigned long)-1), unsigned long ticks=(TICKS2WAIT)) {
        if (!filter) {
#if !defined(BLOCKING_MODE) 
            for(unsigned long i=0;i<retry;++i) {
                if (buffer->push(task)) return true;
                losetime_out(ticks);
            }           
            return false;
#else
            while(!buffer->push(task)) {
                pthread_mutex_lock(&prod_m);
                while(atomic_long_read(&prod_counter) >= buffer->buffersize()) {
                    pthread_cond_wait(&prod_c,&prod_m);
                }
                pthread_mutex_unlock(&prod_m);  
            } 
            push_done();
            return true;
#endif
        }
#if !defined(BLOCKING_MODE)
        for(unsigned long i=0;i<retry;++i) {
            if (filter->push(task)) return true;
            losetime_out();
        }
#else
        while(!filter->push(task)) {
            pthread_mutex_lock(&prod_m);
            while(atomic_long_read(&prod_counter) >= filter->get_out_buffer()->buffersize()) {
                pthread_cond_wait(&prod_c,&prod_m);
            }
            pthread_mutex_unlock(&prod_m);      
        } 
        push_done();
#endif
        return true;
    }

    

    /**
     * \brief Pop a task out of the queue.
     *
     * It pops the task out of the queue.
     *
     * \return \p false if not successful, otherwise \p true is returned.
     *
     */
    bool pop(void ** task) {
        if (!get_out_buffer()) return false;
        while (! buffer->pop(task)) {
            losetime_in();
        } 
        return true;
    }

    /**
     * \brief Pop a task
     *
     * It pops the task.
     *
     * \return The task popped from the buffer.
     */
    bool pop_nb(void ** task) {
        if (!get_out_buffer()) return false;
        return buffer->pop(task);
    }


    static bool ff_send_out_collector(void * task,
                                      unsigned long retry, 
                                      unsigned long ticks, void *obj) {
        return ((ff_gatherer *)obj)->push(task, retry, ticks);
    }

#if defined(FF_TASK_CALLBACK)
    void callbackIn(void  *t=NULL) { filter->callbackIn(t);  }
    void callbackOut(void *t=NULL) { filter->callbackOut(t); }
#endif

public:

    /**
     *  \brief Constructor
     *
     *  It creates \p max_num_workers and \p NULL pointers to worker objects.
     */
    ff_gatherer(int max_num_workers):
        running(-1),max_nworkers(max_num_workers), nextr(0),
        neos(0),neosnofreeze(0),channelid(-1),
        filter(NULL), workers(max_nworkers), offline(max_nworkers), buffer(NULL),
        skip1pop(false) {
        time_setzero(tstart);time_setzero(tstop);
        time_setzero(wtstart);time_setzero(wtstop);
        wttime=0;
#if defined(BLOCKING_MODE)
        atomic_long_set(&cons_counter, -1);
        atomic_long_set(&prod_counter, -1);
        p_cons_m = NULL, p_cons_c = NULL, p_cons_counter = NULL;
#endif

        FFTRACE(taskcnt=0;lostpushticks=0;pushwait=0;lostpopticks=0;popwait=0;ticksmin=(ticks)-1;ticksmax=0;tickstot=0);
    }

    virtual ~ff_gatherer() {}

    /**
     * \brief Sets the filer
     *
     * It sents the \p ff_node to the filter.
     *
     * \return 0 if successful, otherwise a negative value is returned.
     */
    int set_filter(ff_node * f) { 
        if (filter) {
            error("GT, setting collector filter\n");
            return -1;
        }
        filter = f;
        if (filter) 
            filter->registerCallback(ff_send_out_collector, this);
        return 0;
    }

    ff_node *get_filter() const { return (filter==(ff_node*)this)?NULL:filter; }

    /**
     * \brief Sets output buffer
     *
     * It sets the output buffer.
     */
    void set_out_buffer(FFBUFFER * const buff) { buffer=buff;}

    /**
     * \brief Gets the channel id
     *
     * It gets the \p channelid.
     *
     * \return The \p channelid is returned.
     */
    ssize_t get_channel_id() const { return channelid;}

    /**
     * \brief Gets the number of worker threads currently running.
     *
     * It gets the number of threads currently running.
     *
     * \return Number of worker threads
     */
    inline size_t getnworkers() const { return (size_t)(running-neos-neosnofreeze); }

    
    inline size_t getrunning() const { return (size_t)running;}
    

    /**
     * \brief Get the number of workers
     *
     * It returns the number of total workers registered
     *
     * \return Number of worker
     */
    inline size_t getNWorkers() const { return workers.size();}


    /**
     * \brief Skips the first pop
     *
     * It determine whether the first pop should be skipped or not.
     *
     * \return Always \true is returned.
     */
    void skipfirstpop() { skip1pop=true; }

    /**
     * \brief Gets the ouput buffer
     *
     * It gets the output buffer
     *
     * \return \p buffer is returned. 
     */
    FFBUFFER * get_out_buffer() const { return buffer;}

    /**
     * \brief Register the given worker to the list of workers.
     *
     * It registers the given worker to the list of workers.
     *
     * \return 0 if successful, or -1 if not successful.
     */
    int  register_worker(ff_node * w) {
        if (workers.size()>=max_nworkers) {
            error("GT, max number of workers reached (max=%ld)\n",max_nworkers);
            return -1;
        }
        workers.push_back(w);
        return 0;
    }


    /**
     * \brief Initializes the gatherer task.
     *
     * It is a virtual function to initialise the gatherer task.
     *
     * \return It returns the task if successful, otherwise 0 is returned.
     */
    virtual int svc_init() { 
        gettimeofday(&tstart,NULL);
        for(unsigned i=0;i<workers.size();++i)  offline[i]=false;
        if (filter) return filter->svc_init(); 
        return 0;
    }

    /**
     * \brief The gatherer task
     *
     * It is a virtual function to be used as the gatherer task.
     *
     * \return It returns the task.
     */
    virtual void * svc(void *) {
        void * ret  = EOS;
        void * task = NULL;
        bool outpresent  = (get_out_buffer() != NULL);
        bool skipfirstpop = skip1pop;

        // the following case is possible when the collector is a dnode
        if (!outpresent && filter && (filter->get_out_buffer()!=NULL)) {
            outpresent=true;
            set_out_buffer(filter->get_in_buffer());
        }
       
        gettimeofday(&wtstart,NULL);
        do {
            task = NULL;
#if defined(FF_TASK_CALLBACK)
            if (filter) callbackIn(this);
#endif
            if (!skipfirstpop) 
                nextr = gather_task(&task); 
            else skipfirstpop=false;

            if ((task == EOS) || (task == EOSW)) {
                if (filter) filter->eosnotify(workers[nextr]->get_my_id());
                offline[nextr]=true;
                ++neos;
                ret=task;
            } else if (task == EOS_NOFREEZE) {
                if (filter) filter->eosnotify(workers[nextr]->get_my_id());
                offline[nextr]=true;
                ++neosnofreeze;
                ret = task;
            } else {
                FFTRACE(++taskcnt);
                if (filter)  {
                    channelid = workers[nextr]->get_my_id();
                    FFTRACE(ticks t0 = getticks());
                    task = filter->svc(task);

#if defined(TRACE_FASTFLOW)
                    ticks diff=(getticks()-t0);
                    tickstot +=diff;
                    ticksmin=(std::min)(ticksmin,diff);
                    ticksmax=(std::max)(ticksmax,diff);
#endif    
                }

#if defined(FF_TASK_CALLBACK)
                if (filter) callbackOut(this);
#endif

                // if the filter returns NULL we exit immediatly
                if (task == GO_ON) continue;
                if ((task == GO_OUT) || (task == EOS_NOFREEZE) || (task == EOSW) ) {
                    ret = task;
                    break;   // exiting from the loop without sending the task
                } 
                if (!task || (task == EOS)) {
                    ret = EOS;
                    break;
                }                
                if (outpresent) push(task);
            }
        } while((neos<(size_t)running) && (neosnofreeze<(size_t)running));

        // GO_OUT, EOS_NOFREEZE and EOSW are not propagated !
        if (outpresent && ((ret != GO_OUT) && (ret != EOS_NOFREEZE) && (ret != EOSW))) {
            // push EOS
            task = ret;
            push(task);
        }
        if (ret == EOSW) ret = EOS; // EOSW is like an EOS but it is not propagated

        gettimeofday(&wtstop,NULL);
        wttime+=diffmsec(wtstop,wtstart);
        if (neos>=(size_t)running) neos=0;
        if (neosnofreeze>=(size_t)running) neosnofreeze=0;

        return ret;
    }

    /**
     * \brief Finializes the gatherer.
     *
     * It is a virtual function used to finalise the gatherer task.
     *
     */
    virtual void svc_end() {
        if (filter) filter->svc_end();
        gettimeofday(&tstop,NULL);
    }

    /**
     * \brief Execute the gatherer task.
     *
     * It executes the gatherer task.
     *
     * \return 0 if successful, otherwise -1 is returned.
     */
    int run(bool=false) {  
        if (this->spawn(filter?filter->getCPUId():-1)== -2) {
            error("GT, spawning GT thread\n");
            return -1; 
        }
        running = workers.size();
        return 0;
    }


    inline int wait_freezing() {
        int r = ff_thread::wait_freezing();
        running = -1;
        return r;
    }

    /**
     *
     * \brief It gathers all tasks.
     *
     * It is a virtual function, and gathers results from the workers. 
     *
     * \return It returns 0 if the tasks from all the workers are collected.
     * Otherwise a negative value is returned.
     *
     */
    virtual int all_gather(void *task, void **V) {
        V[channelid]=task;
        size_t nw=getnworkers();
        svector<ff_node*> _workers(nw);
        for(ssize_t i=0;i<running;++i) 
            if (!offline[i]) _workers.push_back(workers[i]);
        svector<size_t> retry(nw);

        for(size_t i=0;i<nw;++i) {
            if(i!=(size_t)channelid) {
                if (!_workers[i]->get(&V[i])) {
                    retry.push_back(i);
                }            
#if defined(BLOCKING_MODE)
                else
                    get_done(i);
#endif
            }
        }
        while(retry.size()) {
            channelid = retry.back();
            if(_workers[channelid]->get(&V[channelid])) {
#if defined(BLOCKING_MODE)
                get_done(channelid);
#endif
                retry.pop_back();
            }
            else {
#if !defined(BLOKING_MODE)
                losetime_in();
#else
                pthread_mutex_lock(&cons_m);
                while(atomic_long_read(&cons_counter) == 0)
                    pthread_cond_wait(&cons_c, &cons_m);
                pthread_mutex_unlock(&cons_m);
#endif
            }
        }
        for(size_t i=0;i<nw;++i)
            if (V[i] == EOS || V[i] == EOS_NOFREEZE)
                return -1;
        FFTRACE(taskcnt+=nw-1);
        return 0;
    }

    /**
     * \brief Thaws all threads register with the gt and the gt itself
     *
     * 
     */
    inline void thaw(bool _freeze=false, ssize_t nw=-1) {
        assert(running==-1);
        if (nw == -1 || (size_t)nw > workers.size()) running = workers.size();
        else running = nw;
        ff_thread::thaw(_freeze);
    }

    /**
     *  \brief Resets output buffer
     *  
     *   Warning resetting the buffer while the node is running may produce unexpected results.
     */
    void reset() { if (buffer) buffer->reset();}


    /**
     * \brief Start counting time
     *
     * It defines the counting of start time.
     *
     * \return Difference in milli seconds.
     */
    virtual double ffTime() {
        return diffmsec(tstop,tstart);
    }

    /**
     * \brief Complete counting time
     *
     * It defines the counting of finished time.
     *
     * \return Difference in milli seconds.
     */
    virtual double wffTime() {
        return diffmsec(wtstop,wtstart);
    }

    virtual const struct timeval & getstarttime() const { return tstart;}
    virtual const struct timeval & getstoptime()  const { return tstop;}
    virtual const struct timeval & getwstartime() const { return wtstart;}
    virtual const struct timeval & getwstoptime() const { return wtstop;}


#if defined(TRACE_FASTFLOW)  
    /**
     * \brief The trace of FastFlow
     *
     * It prints the trace for FastFlow.
     *
     */
    virtual void ffStats(std::ostream & out) { 
        out << "Collector: "
            << "  work-time (ms): " << wttime    << "\n"
            << "  n. tasks      : " << taskcnt   << "\n"
            << "  svc ticks     : " << tickstot  << " (min= " << (filter?ticksmin:0) << " max= " << ticksmax << ")\n"
            << "  n. push lost  : " << pushwait  << " (ticks=" << lostpushticks << ")" << "\n"
            << "  n. pop lost   : " << popwait   << " (ticks=" << lostpopticks  << ")" << "\n";
    }
#endif

private:
    ssize_t           running;       /// Number of workers running
    size_t            max_nworkers;
    ssize_t           nextr;

    size_t            neos;
    size_t            neosnofreeze;
    ssize_t           channelid;

    ff_node         * filter;
    svector<ff_node*> workers;
    svector<bool>     offline;
    FFBUFFER        * buffer;
    bool              skip1pop;

    struct timeval tstart;
    struct timeval tstop;
    struct timeval wtstart;
    struct timeval wtstop;
    double wttime;

protected:

#if defined(BLOCKING_MODE)
    // for the input queue
    pthread_mutex_t cons_m;
    pthread_cond_t  cons_c;
    atomic_long_t   cons_counter;

    // for the output queue
    pthread_mutex_t prod_m;
    pthread_cond_t  prod_c;
    atomic_long_t   prod_counter;

    // for synchronizing with the next multi-input stage
    pthread_mutex_t *p_cons_m;
    pthread_cond_t  *p_cons_c;
    atomic_long_t   *p_cons_counter;
#endif    

#if defined(TRACE_FASTFLOW)
    unsigned long taskcnt;
    ticks         lostpushticks;
    unsigned long pushwait;
    ticks         lostpopticks;
    unsigned long popwait;
    ticks         ticksmin;
    ticks         ticksmax;
    ticks         tickstot;
#endif
};


} // namespace ff

#endif /* FF_GT_HPP */
