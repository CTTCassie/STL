#define _CRT_SECURE_NO_WARNINGS 1
#include<iostream>
using namespace std;
#include<stdio.h>
#include<windows.h>
#include<vector>

// Trace ���� 
#define __DEBUG__  
FILE* fout = fopen("log.log", "w");   //��һ��log.log�ļ������Խ���Ϣ��Ϣд�����ļ���
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
//���ڵ���׷�ݵ�trace log  
inline static void __trace_debug(const char* function, const char * filename, int line, char* format, ...)
{
	// ��ȡ�����ļ�  
#ifdef __DEBUG__  
	// ������ú�������Ϣ  
	fprintf(stdout, "��%s:%d��%s", GetFileName(filename).c_str(), line, function);

	fprintf(fout, "��%s:%d��%s", GetFileName(filename).c_str(), line, function);
	// ����û����trace��Ϣ  
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



//һ���ռ�������
typedef void(*HANDLER_FUNC)();   //��void(*)()������ΪHANDLER_FUNC

template <int inst>
class __MallocAllocTemplate
{
private:
	static HANDLER_FUNC __Malloc_Alloc_OOM_Handler;  //����ָ�룬�ڴ治��ʱ�Ĵ������

	static void *OOM_Malloc(size_t n)  //mallocʧ�ܵ�ʱ����õĺ���
	{
		__TRACE_DEBUG("һ���ռ�����������%u���ֽ�ʧ�ܣ������ڴ治�㴦����", n);
		void *result = NULL;
		while (1)
		{
			if (__Malloc_Alloc_OOM_Handler == 0)
			{
				//û�������ڴ治��ʱ�Ĵ����������׳�bad_alloc�쳣
				throw std::bad_alloc();
			}
			__Malloc_Alloc_OOM_Handler(); //�����ڴ治�㴦���������봦������λ�õ��ڴ�
			if (result = malloc(n))   //���������ڴ棬����ɹ���������ѭ��
			{
				break;
			}
		}
		return result;
	}
public:
	static void * Allocate(size_t n)
	{
		__TRACE_DEBUG("һ���ռ����������ٿռ䣺%u��", n);
		void *result = malloc(n);
		if (0 == result)
		{
			//mallocʧ�ܣ������ڴ治�㴦����
			result = OOM_Malloc(n);
		}
		return result;
	}
	static void Deallocate(void *ptr)
	{
		__TRACE_DEBUG("һ���ռ��������ͷſռ�0#x%p", ptr);
		free(ptr);
		ptr = NULL;
	}
	static HANDLER_FUNC SetMallocHandler(HANDLER_FUNC f)
	{
		__TRACE_DEBUG("һ���ռ�����������ʧ�����þ��");
		HANDLER_FUNC old = __Malloc_Alloc_OOM_Handler;
		__Malloc_Alloc_OOM_Handler = f;  //���ڴ����ʧ�ܵľ������Ϊf(����ָ��һ���ڴ�ʧ���ˣ���ϵͳȥ�ͷ������ط��ռ�ĺ���) 
		return old;
	}
};

template<int inst>
void(*__MallocAllocTemplate<inst>::__Malloc_Alloc_OOM_Handler)() = 0;   //Ĭ�ϲ������ڴ治�㴦����




//�����ռ�������
enum { __ALIGN = 8 };     //��׼ֵ��8�ı���
enum { __MAX_BYTES = 128 };    //�������������Ŀ�Ĵ�С��128
enum { __NFREELISTS = __MAX_BYTES / __ALIGN };  //��������ĳ���16

template<bool threads, int inst>
class __DefaultAllocTemplate
{
private:
	union Obj    //��������Ľ������
	{
		union Obj * FreeListLink;  //ָ����������Ľ��ָ��
		char ClientData[1];     //�ͻ�������
	};
	static char *startFree;    //ָ���ڴ�ص�ͷָ��
	static char *endFree;      //ָ���ڴ�ص�βָ��
	static size_t heapSize;    //������¼ϵͳ�Ѿ����ڴ�������˶��Ŀռ�
	static Obj* volatile FreeList[__NFREELISTS]; //��������
private:
	static  size_t FREELIST_INDEX(size_t bytes)  //���Ӧ���������ϵĶ�Ӧ�±�
	{
		return (((bytes)+__ALIGN - 1) / __ALIGN - 1);

		//if (bytes % 8 == 0)
		//	return bytes / 8 - 1;
		//else
		//	return bytes / 8;
	}
	static size_t ROUND_UP(size_t bytes)  //��bytes���ϵ�����8�ı���
	{
		return (((bytes)+__ALIGN - 1) & ~(__ALIGN - 1));
	}
	static char *ChunkAlloc(size_t size, int& nobjs)   //��ϵͳ�������ڴ�
	{
		char *result = NULL;
		size_t totalBytes = size*nobjs;  //�ܹ�������ڴ��Ĵ�С
		size_t leftBytes = endFree - startFree;  //�ڴ��ʣ��Ĵ�С
		if (leftBytes >= totalBytes)   //�ڴ��ʣ��ռ���ȫ��������
		{
			__TRACE_DEBUG("��%u���ֽڣ�����������Ҫ��%u���ֽڣ����Է���%u������", leftBytes, totalBytes, nobjs);
			result = startFree;
			startFree += totalBytes;
			return result;
		}
		else if (leftBytes >= size)  //�ڴ���е�ʣ��ռ�ֻ���Թ�Ӧһ�����ϵ����飬���ǲ�����ȫ��������
		{
			__TRACE_DEBUG("��%u���ֽڣ�����������Ҫ��%u���ֽڣ�ֻ�ܷ���%u������", leftBytes, totalBytes, nobjs);
			nobjs = leftBytes / size;  //�����һ������Ĵ�С
			totalBytes = size*nobjs;   //�ټ��������Ҫ���ܴ�С
			result = startFree;
			startFree += totalBytes;
			return result;
		}
		else   //�ڴ���еĴ�С�Ѿ���һ������Ĵ�С���޷�������
		{
			//ȥ��������һ��ռ�
			size_t BytesToGet = 2 * totalBytes + ROUND_UP(heapSize>>4);  //������Ҫ��õ��ڴ��Ĵ�С
			if (leftBytes > 0)  //�ڴ���л����ʵ����ڴ棬����ŵ�����������ȥ
			{
				size_t index = FREELIST_INDEX(leftBytes);

				__TRACE_DEBUG("�ڴ���л����ڴ棬�������������������%uλ����", index);

				Obj* volatile* myFreeList = FreeList + index;
				//���������������ͷ��
				((Obj*)startFree)->FreeListLink = *myFreeList;
				*myFreeList = ((Obj*)startFree);
			}
			//���¿��ٿռ䣬���������ڴ��
			startFree = (char *)malloc(BytesToGet);
			__TRACE_DEBUG("�ڴ����û���ڴ棬malloc����%u���ֽ�", BytesToGet);
			if (startFree == 0)
			{
				//ϵͳ����ʧ��������������л�ã�������������л�û�еĻ�ֻ�ܵ���һ���ռ���������
				__TRACE_DEBUG("malloc����%u���ֽ�ʧ��", BytesToGet);
				for (int i = size; i < __MAX_BYTES; i += __ALIGN)
				{
					size_t index = FREELIST_INDEX(i);
					Obj* volatile *myFreeList = FreeList + index;
					Obj* ptr = *myFreeList;
					if (ptr != NULL)  //�������������ҵ�һ���ڴ��
					{
						__TRACE_DEBUG("�����������%uλ���ҵ�һ���ڴ��", index);
						//ȡ��һ�飬ͷɾ
						startFree = (char *)ptr;
						*myFreeList = ptr->FreeListLink;
						endFree = startFree + i;
						return ChunkAlloc(size,nobjs);
					}
				}
				//������������û���ҵ������һ���ռ�������
				endFree = 0;   //��ֹ�쳣
				__TRACE_DEBUG("mallocû������ɹ���������������Ҳû���ҵ��յĻ���飬����һ���ռ�����������");

				startFree = (char *)__MallocAllocTemplate<0>::Allocate(BytesToGet);
			}
			//malloc�ɹ��������heapSize������endFree
			__TRACE_DEBUG("�ռ�����ɹ�");
			heapSize += BytesToGet;
			endFree = startFree + BytesToGet;
			return ChunkAlloc(size,nobjs);

		}
	}
	static void *Refile(size_t n)   //n��Ҫ�����ֽڵĸ���
	{
		int nobjs = 20;   //һ�������ڴ������20���ռ�

		__TRACE_DEBUG("���������%uλ��û�пյĻ����ɹ�ʹ�ã����%u��", FREELIST_INDEX(n), nobjs);

		char *chunk = ChunkAlloc(n, nobjs);  //���ڴ�����������ڴ�
		if (nobjs == 1)  //ֻ���䵽��һ������
		{
			return chunk;
		}
		Obj *res = (Obj *)chunk;
		Obj* volatile *myFreeList = (FreeList + FREELIST_INDEX(n));
		*myFreeList = (Obj *)(chunk+n);
		Obj *cur = *myFreeList;
		Obj *next = 0;
		cur->FreeListLink = 0;
		//��ʣ�µ��ڴ��ҵ�����������
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
	static void * Allocate(size_t n)   //����ռ�
	{
		void *res;
		Obj* volatile *myFreeList;
		__TRACE_DEBUG("�����ռ�����������%u���ֽڵĿռ�", n);
		if (n > __MAX_BYTES)   //����__MAX_BYTES�ֽڵ�����Ϊ�Ǵ���ڴ棬ֱ�ӵ���һ���ռ�������
		{
			__TRACE_DEBUG("�����ռ�����������һ���ռ�������");
			return (__MallocAllocTemplate<inst>::Allocate(n));
		}
		//ȥ���������в���
		myFreeList = FreeList + FREELIST_INDEX(n); //myFreeListָ���������ȡ��8��������
		res = *myFreeList;
		if (res != NULL)
		{
			__TRACE_DEBUG("���������%uλ�û��пյĻ����ɹ�ʹ��", FREELIST_INDEX(n));
			*myFreeList = ((Obj *)res)->FreeListLink;
			return res;
		}
		//����������û�пյĻ����
		return Refile(ROUND_UP(n));
	}
	static void Deallocate(void *ptr, size_t n)
	{
		if (n > __MAX_BYTES)//���n�������������н�����ܹҵ�����ڴ�飬��ֱ�ӵ����Լ��ռ����������ͷ��ڴ�ĺ��� 
		{
			__TRACE_DEBUG("����һ���ռ����������ͷſռ�ĺ���");
			__MallocAllocTemplate<0>::Deallocate(ptr);
		}
		else  //��������ڴ���յ�����������ȥ
		{
			size_t index = FREELIST_INDEX(n);

			__TRACE_DEBUG("�����ڴ浽���������е�%u��λ�ô�",index);
			//ͷ��
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
//ǰ���typename�������������һ������
//��������ά���ĳ�����16
typename __DefaultAllocTemplate<threads, inst>::Obj *volatile 
__DefaultAllocTemplate<threads, inst>::FreeList[__NFREELISTS] = 
{ 0 ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

//����һ��ķ���ռ�ĳ���
void TestAlloc()
{
	__MallocAllocTemplate<0>::Allocate(128);
	__DefaultAllocTemplate<false, 0> d;
	char *p2 = (char *)d.Allocate(130);
	char *p1 = (char *)d.Allocate(8);
}

//���Զ����ռ��������Ŀ��ٺ��ͷſռ�
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
	//�ͷſռ�
	while (!arr.empty())
	{
		d.Deallocate(arr.back().first, arr.back().second);
		arr.pop_back();
	}
	int end = GetTickCount();
	cout << end - start << endl;
}