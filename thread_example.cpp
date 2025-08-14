#include "thread_utils.h"
using namespace std;

void dummyFunction(int a, int b, bool sleep){
    cout << "dummyFunction(" << a << "," << b << ")" << endl;
    cout << "dummyFunction output=" << a + b << endl;
    if(sleep){
        cout<<"Dummy function sleeping..."<<endl;
        using namespace literals::chrono_literals;
        this_thread::sleep_for(5s);
    }
    cout<<"Dummy function done."<<endl;
}

int main(){
    using namespace Common;

    auto t1 = createAndStartThread(-1, "dummyFunction1", dummyFunction, 12, 21, false);
    auto t2 = createAndStartThread(-1, "dummyFunction2", dummyFunction, 15, 51, true);

    cout<<"Main waiting for the threads to be done.\n";
    t1->join();
    t2->join();
    cout<<"Main exiting\n";
    return 0;
}
