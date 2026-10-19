#pragma once

namespace mss {

// 纯 Arena：只承载可复用的临时内存，不作为传信息的方式。
// 只能在所属函数内部使用；调用之间不保留有效数据，也不得从上次调用的残留读取输入。
struct Workspace {};

} // 命名空间 mss
