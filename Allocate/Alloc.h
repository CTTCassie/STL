#define _CRT_SECURE_NO_WARNINGS 1
#include<iostream>
using namespace std;
#include<stdio.h>
#include<windows.h>
#include<vector>

// Trace 跟踪 
#define __DEBUG__  
FILE* fout = fopen("log.log", "w");   //打开一个log.log文件，可以将信息信息写进该文件中
static string GetFileName(const string& path)
{
	char ch = '/';
#ifdef _WIN32  
	ch = '\\';
#endif  
	size_t pos = path.rfind(ch);
	if (pos == string::npos)
		return path;
	else
		return path.substr(pos + 1);
}
//用于调试追溯的trace log  
inline static void __trace_debug(const char* function, const char * filename, int line, char* format, ...)
{
	// 读取配置文件  
#ifdef __DEBUG__  
	// 输出调用函数的信息  
	fprintf(stdout, "【%s:%d】%s", GetFileName(filename).c_str(), line, function);

	fprintf(fout, "【%s:%d】%s", GetFileName(filename).c_str(), line, function);
	// 输出用户打的trace信息  
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);

	vfprintf(fout, format, args);
	fprintf(fout, "%c", '\n');
	fprintf(stdout, "%c", '\n');
	va_end(args);
#endif  
}

#define __TRACE_DEBUG(...)  __trace_debug(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);



//一级空间配置器
typedef void(*HANDLER_FUNC)();   //把void(*)()重命名为HANDLER_FUNC

template <int inst>
class __MallocAllocTemplate
{
private:
	static HANDLER_FUNC __Malloc_Alloc_OOM_Handler;  //函数指针，内存不足时的处理机制

	static void *OOM_Malloc(size_t n)  //malloc失败的时候调用的函数
	{
		__TRACE_DEBUG("一级空间配置器申请%u个字节失败，调用内存不足处理函数", n);
		void *result = NULL;
		while (1)
		{
			if (__Malloc_Alloc_OOM_Handler == 0)
			{
				//没有设置内存不足时的处理函数，则抛出bad_alloc异常
				throw std::bad_alloc();
			}
			__Malloc_Alloc_OOM_Handler(); //调用内存不足处理函数，申请处理其他位置的内存
			if (result = malloc(n))   //重新申请内存，申请成功则跳出该循环
			{
				break;
			}
		}
		return result;
	}
public:
	static void * Allocate(size_t n)
	{
		__TRACE_DEBUG("一级空间配置器开辟空间：%u个", n);
		void *result = malloc(n);
		if (0 == result)
		{
			//malloc失败，调用内存不足处理函数
			result = OOM_Malloc(n);
		}
		return result;
	}
	static void Deallocate(void *ptr)
	{
		__TRACE_DEBUG("一级空间配置器释放空间0#x%p", ptr);
		free(ptr);
		ptr = NULL;
	}
	static HANDLER_FUNC SetMallocHandler(HANDLER_FUNC f)
	{
		__TRACE_DEBUG("一级空间配置器分配失败设置句柄");
		HANDLER_FUNC old = __Malloc_Alloc_OOM_Handler;
		__Malloc_Alloc_OOM_Handler = f;  //将内存分配失败的句柄设置为f(让它指向一个内存失败了，让系统去释放其他地方空间的函数) 
		return old;
	}
};

template<int inst>
void(*__MallocAllocTemplate<inst>::__Malloc_Alloc_OOM_Handler)() = 0;   //默认不设置内存不足处理函数




//二级空间配置器
enum { __ALIGN = 8 };     //基准值是8的倍数
enum { __MAX_BYTES = 128 };    //自由链表中最大的块的大小是128
enum { __NFREELISTS = __MAX_BYTES / __ALIGN };  //自由链表的长度16

template<bool threads, int inst>
class __DefaultAllocTemplate
{
private:
	union Obj    //自由链表的结点类型
	{
		union Obj * FreeListLink;  //指向自由链表的结点指针
		char ClientData[1];     //客户端数据
	};
	static char *startFree;    //指向内存池的头指针
	static char *endFree;      //指向内存池的尾指针
	static size_t heapSize;    //用来记录系统已经向内存池申请了多大的空间
	static Obj* volatile FreeList[__NFREELISTS]; //自由链表
private:
	static  size_t FREELIST_INDEX(size_t bytes)  //求对应自由链表上的对应下标
	{
		return (((bytes)+__ALIGN - 1) / __ALIGN - 1);

		//if (bytes % 8 == 0)
		//	return bytes / 8 - 1;
		//else
		//	return bytes / 8;
	}
	static size_t ROUND_UP(size_t bytes)  //将bytes向上调整至8的倍数
	{
		return (((bytes)+__ALIGN - 1) & ~(__ALIGN - 1));
	}
	static char *ChunkAlloc(size_t size, int& nobjs)   //向系统中申请内存
	{
		char *result = NULL;
		size_t totalBytes = size*nobjs;  //总共申请的内存块的大小
		size_t leftBytes = endFree - startFree;  //内存池剩余的大小
		if (leftBytes >= totalBytes)   //内存池剩余空间完全满足容量
		{
			__TRACE_DEBUG("有%u个字节，足以满足需要的%u个字节，可以分配%u个对象", leftBytes, totalBytes, nobjs);
			result = startFree;
			startFree += totalBytes;
			return result;
		}
		else if (leftBytes >= size)  //内存池中的剩余空间只可以供应一个以上的区块，但是不能完全满足需求
		{
			__TRACE_DEBUG("有%u个字节，不能满足需要的%u个字节，只能分配%u个对象", leftBytes, totalBytes, nobjs);
			nobjs = leftBytes / size;  //计算出一个对象的大小
			totalBytes = size*nobjs;   //再计算出所需要的总大小
			result = startFree;
			startFree += totalBytes;
			return result;
		}
		else   //内存池中的大小已经连一个区块的大小都无法满足了
		{
			//去堆上申请一块空间
			size_t BytesToGet = 2 * totalBytes + ROUND_UP(heapSize>>4);  //计算想要获得的内存块的大小
			if (leftBytes > 0)  //内存池中还有适当的内存，将其放到自由链表上去
			{
				size_t index = FREELIST_INDEX(leftBytes);

				__TRACE_DEBUG("内存池中还有内存，它将被挂在自由链表的%u位置下", index);

				Obj* volatile* myFreeList = FreeList + index;
				//将其挂入自由链表，头插
				((Obj*)startFree)->FreeListLink = *myFreeList;
				*myFreeList = ((Obj*)startFree);
			}
			//重新开辟空间，用来补充内存池
			startFree = (char *)malloc(BytesToGet);
			__TRACE_DEBUG("内存池中没有内存，malloc申请%u个字节", BytesToGet);
			if (startFree == 0)
			{
				//系统开辟失败则从自由链表中获得，如果自由链表中还没有的话只能调用一级空间配置器了
				__TRACE_DEBUG("malloc申请%u个字节失败", BytesToGet);
				for (int i = size; i < __MAX_BYTES; i += __ALIGN)
				{
					size_t index = FREELIST_INDEX(i);
					Obj* volatile *myFreeList = FreeList + index;
					Obj* ptr = *myFreeList;
					if (ptr != NULL)  //在自由链表中找到一块内存块
					{
						__TRACE_DEBUG("在自由链表的%u位置找到一块内存块", index);
						//取出一块，头删
						startFree = (char *)ptr;
						*myFreeList = ptr->FreeListLink;
						endFree = startFree + i;
						return ChunkAlloc(size,nobjs);
					}
				}
				//在自由链表中没有找到则调用一级空间配置器
				endFree = 0;   //防止异常
				__TRACE_DEBUG("malloc没有申请成功，在自由链表中也没有找到空的缓冲块，调用一级空间配置器分配");

				startFree = (char *)__MallocAllocTemplate<0>::Allocate(BytesToGet);
			}
			//malloc成功，则更新heapSize，更新endFree
			__TRACE_DEBUG("空间申请成功");
			heapSize += BytesToGet;
			endFree = startFree + BytesToGet;
			return ChunkAlloc(size,nobjs);

		}
	}
	static void *Refile(size_t n)   //n是要申请字节的个数
	{
		int nobjs = 20;   //一次性向内存池申请20个空间

		__TRACE_DEBUG("自由链表的%u位置没有空的缓冲块可供使用，填充%u个", FREELIST_INDEX(n), nobjs);

		char *chunk = ChunkAlloc(n, nobjs);  //到内存池中申请大块内存
		if (nobjs == 1)  //只分配到了一个对象
		{
			return chunk;
		}
		Obj *res = (Obj *)chunk;
		Obj* volatile *myFreeList = (FreeList + FREELIST_INDEX(n));
		*myFreeList = (Obj *)(chunk+n);
		Obj *cur = *myFreeList;
		Obj *next = 0;
		cur->FreeListLink = 0;
		//将剩下的内存块挂到自由链表上
		for (int i = 2; i < nobjs; ++i)
		{
			next = (Obj *)(chunk+n);
			cur->FreeListLink = next;
			cur = next;
		}
		cur->FreeListLink = 0;
		return res;
	}
public:
	static void * Allocate(size_t n)   //分配空间
	{
		void *res;
		Obj* volatile *myFreeList;
		__TRACE_DEBUG("二级空间配置器开辟%u个字节的空间", n);
		if (n > __MAX_BYTES)   //大于__MAX_BYTES字节的则认为是大块内存，直接调用一级空间配置器
		{
			__TRACE_DEBUG("二级空间配置器调用一级空间配置器");
			return (__MallocAllocTemplate<inst>::Allocate(n));
		}
		//去自由链表中查找
		myFreeList = FreeList + FREELIST_INDEX(n); //myFreeList指向的是向上取的8的整数倍
		res = *myFreeList;
		if (res != NULL)
		{
			__TRACE_DEBUG("自由链表的%u位置还有空的缓冲块可供使用", FREELIST_INDEX(n));
			*myFreeList = ((Obj *)res)->FreeListLink;
			return res;
		}
		//自由链表中没有空的缓冲块
		return Refile(ROUND_UP(n));
	}
	static void Deallocate(void *ptr, size_t n)
	{
		if (n > __MAX_BYTES)//如果n大于自由链表中结点所能挂的最大内存块，则直接调用以及空间配置器的释放内存的函数 
		{
			__TRACE_DEBUG("调用一级空间配置器的释放空间的函数");
			__MallocAllocTemplate<0>::Deallocate(ptr);
		}
		else  //否则将这块内存回收到自由链表中去
		{
			size_t index = FREELIST_INDEX(n);

			__TRACE_DEBUG("回收内存到自由链表中的%u的位置处",index);
			//头插
			Obj *tmp = (Obj *)ptr;
			Obj* volatile *myFreeList = FreeList + index;
			tmp->FreeListLink = *myFreeList;
			*myFreeList = tmp;
		}
	}
};

template<bool threads, int inst>
char *__DefaultAllocTemplate<threads, inst>::startFree = 0;

template<bool threads, int inst>
char *__DefaultAllocTemplate<threads, inst>::endFree = 0;

template<bool threads, int inst>
size_t __DefaultAllocTemplate<threads, inst>::heapSize = 0;

template<bool threads, int inst>
//前面加typename表明它后面的是一个类型
//自由链表维护的长度是16
typename __DefaultAllocTemplate<threads, inst>::Obj *volatile 
__DefaultAllocTemplate<threads, inst>::FreeList[__NFREELISTS] = 
{ 0 ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

//测试一般的分配空间的场景
void TestAlloc()
{
	__MallocAllocTemplate<0>::Allocate(128);
	__DefaultAllocTemplate<false, 0> d;
	char *p2 = (char *)d.Allocate(130);
	char *p1 = (char *)d.Allocate(8);
}

//测试二级空间配置器的开辟和释放空间
void TestDefaultAllocTemplate()
{
	int start = GetTickCount();
	__DefaultAllocTemplate<false, 0> d;
	vector<pair<void *, int>> arr;
	arr.push_back(make_pair(d.Allocate(130),130));
	for (int i = 0; i < 100; i++)
	{
		arr.push_back(make_pair(d.Allocate(30),i));
	}
	while (!arr.empty())
	{
		d.Deallocate(arr.back().first,arr.back().second);
		arr.pop_back();
	}
	for (int i = 0; i < 100; i++)
	{
		arr.push_back(make_pair(d.Allocate(30), i));
	}
	//释放空间
	while (!arr.empty())
	{
		d.Deallocate(arr.back().first, arr.back().second);
		arr.pop_back();
	}
	int end = GetTickCount();
	cout << end - start << endl;
}