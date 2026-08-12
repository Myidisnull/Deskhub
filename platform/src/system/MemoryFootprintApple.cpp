#include "deskhubp/system/MemoryFootprint.h"

#include <mach/mach.h>

namespace deskhubp {
namespace {

constexpr mach_vm_size_t kBytesPerMb = mach_vm_size_t{1024} * 1024;

}

int MemoryFootprintMb() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&info), &count);
    if (kr != KERN_SUCCESS) return -1;
    return int(info.phys_footprint / kBytesPerMb);
}

}
