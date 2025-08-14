#include "thread_utils.h"
#include "lf_queue.h"

using namespace  std;

struct MyStruct {
    int d_[3];
};

using namespace Common;

auto consumeFunction(LFQueue<MyStruct>* lfq){
    using namespace literals::chrono_literals;
    this_thread::sleep_for(5s);

    while(lfq->size()){
        const auto d = lfq->getNextRead();
        lfq->updateReadIndex();
        cout << "consumeFunction read elem:" << d->d_[0] << "," << d->d_[1] << "," << d->d_[2] << " lfq-size:" << lfq->size() << endl;
        this_thread::sleep_for(1s);
    }

    cout<<"consumeFunction exiting."<<endl;
}

int main() {
    LFQueue<MyStruct> lfq(20);

    auto ct = createAndStartThread(-1, "", consumeFunction, &lfq);

    for(auto i=0; i<50; ++i){
        const MyStruct d{i, i*10, i*100};
        *(lfq.getNextWriteTo()) = d;
        lfq.updateWriteIndex();

        std::cout << "main constructed elem:" << d.d_[0] << "," << d.d_[1] << "," << d.d_[2] << " lfq-size:" << lfq.size() << std::endl;

        using namespace literals::chrono_literals;
        this_thread::sleep_for(1s);
    }

    ct->join();
    cout<<"main exiting."<<endl;
    return 0;
}
