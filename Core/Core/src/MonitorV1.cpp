#include "MonitorV1.h"
#include <vector>
#include <tchar.h>
#include <psapi.h>
#include <string>
#pragma comment(lib, "pdh")
#pragma comment(lib, "Kernel32")
using namespace Core;
//-- MEMORYS --//

const wchar_t* MONITOR_MEMORY_CACHE_FAULT_PER_SEC = L"\\Memory\\Cache Faults/sec";
const wchar_t* MONITOR_MEMORY_COMMITED_BYTES = L"\\Memory\\Committed Bytes";
const wchar_t* MONITOR_MEMORY_NONPAGED_BYTES = L"\\Memory\\Pool Nonpaged Bytes";
const wchar_t* MONITOR_MEMORY_AVAILABLE_BYTES = L"\\Memory\\Available Bytes";

//-- NETWORKS --//
const wchar_t* MONITOR_NETWORK_SEGMENTS_RECVED = L"\\TCPv4\\Segments Received/sec";
const wchar_t* MONITOR_NETWORK_SEGMENTS_SENT = L"\\TCPv4\\Segments Sent/sec";
const wchar_t* MONITOR_NETWORK_SEGMENTS_RESENT = L"\\TCPv4\\Segments Retransmitted/sec";

//-- PROCESS --//
const wchar_t* MONITOR_PROCESS_NONPAGED_BYTES = L"\\Process(%s)\\Pool Nonpaged Bytes";
const wchar_t* MONITOR_PROCESS_PRIVATE_BYTES = L"\\Process(%s)\\Private Bytes";
const wchar_t* MONITOR_GET_INSTANCE_NAME = L"\\Process(%s)\\ID Process";

// 추가 -------------
const wchar_t* MONITOR_NETWORK_BYTES_RECVED_FORMAT = L"\\Network Adapter(%s)\\Bytes Received/sec";
const wchar_t* MONITOR_NETWORK_BYTES_SENT_FORMAT = L"\\Network Adapter(%s)\\Bytes Sent/sec";

/////////////////////////////////////////////////////////////////////////////
// 프로세스 모니터링 V1
/////////////////////////////////////////////////////////////////////////////

CMonitorProcess::CMonitorProcess(HANDLE hProcess):
	_fProcessTotal(0.0f), _fProcessUser(0.0f), _fProcessKernel(0.0f)
	, _lldProcessNonpagedBytes(0), _lldProcessPrivateBytes(0)
{
	//-----------------------------------------------------
	// 0. 프로세스 입력이 없다면 자신을 등록
	//-----------------------------------------------------
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		_hProcess = GetCurrentProcess();
	}
	else
	{
		_hProcess = hProcess;
	}

	//-----------------------------------------------------
	// 1. 프로세서 개수를 확인 
	// . 실행률 계산시 cpu개수로 나누려고
	//-----------------------------------------------------
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	_iNumberOfProcessors = (int)sysInfo.dwNumberOfProcessors;

	_ftProcess_LastKernel.QuadPart = 0;
	_ftProcess_LastUser.QuadPart = 0;
	_ftProcess_LastTime.QuadPart = 0;

	//-----------------------------------------------------
	// 2. 자신의 이름 얻어오기
	// 1) 인스턴스 뽑아옴
	//-----------------------------------------------------
	PDH_STATUS status;
	DWORD size = 0;
	DWORD instanceLen = 0;
	status = PdhEnumObjectItemsW(NULL, NULL, L"Process", NULL, &size, NULL, &instanceLen, PERF_DETAIL_WIZARD, 0);
	std::vector<wchar_t> buffer(size);
	std::vector<wchar_t> instanceBuf(instanceLen);
	status = PdhEnumObjectItemsW(NULL, NULL, L"Process", buffer.data(), &size, instanceBuf.data(), &instanceLen, PERF_DETAIL_WIZARD, 0);
	
	//-----------------------------------------------------
	// 2) 프로세스 id 와 매칭해 이름을 뽑는다.
	//-----------------------------------------------------
	
	//-----------------------------------------------------
	// 현재 파일 이름을 얻어서 exe를 뗌
	//-----------------------------------------------------
	DWORD pid = GetProcessId(_hProcess);
	wchar_t wszName[256];
	K32GetModuleBaseNameW(_hProcess, NULL, wszName, 256);
	wchar_t* pName = wszName;
	while (*pName != '.')
		pName++;
	*pName = L'\0';
	
	//-----------------------------------------------------
	// 인스턴스 목록을 받아와서 현재 파일 이름과 동일하면
	// 등록
	//-----------------------------------------------------
	wchar_t* pBuf = instanceBuf.data();
	wchar_t querystring[256];
	PDH_HQUERY hQuery;
	std::vector<PDH_HCOUNTER> hCounters;
	std::vector<std::wstring> pInstanceNames;
	int sameNameCounter = 0;

	PdhOpenQueryW(NULL, 0, &hQuery);
	while (*pBuf != L'\0')
	{
		//-------------------------------------------------
		// 이름이 같으면 추가
		//-------------------------------------------------
		if(wcscmp(wszName, pBuf)==0)
		{
			std::wstring name = wszName;
			name += L'#';
			name += std::to_wstring(sameNameCounter++);
			pInstanceNames.push_back(name);
			PDH_HCOUNTER hCounter;
			swprintf_s(querystring, MONITOR_GET_INSTANCE_NAME, name.c_str());
			PdhAddCounterW(hQuery, querystring, 0, &hCounter);
			hCounters.push_back(hCounter);
		}
		pBuf += (wcslen(pBuf) + 1);
	}
	PdhCollectQueryData(hQuery);
	for (int i = 0; i < hCounters.size(); i++)
	{
		PDH_FMT_COUNTERVALUE counter;
		PdhGetFormattedCounterValue(hCounters[i], PDH_FMT_LONG, NULL, &counter);
		if (counter.longValue == pid)
		{
			wcscpy_s(_wszName, pInstanceNames[i].c_str());
			break;
		}
	}
	for (int i = 0; i < hCounters.size(); i++)
	{
		PdhRemoveCounter(hCounters[i]);
	}
	PdhCloseQuery(hQuery);	

	//----------------------------------------------------
	// 3) 이름이 없으면 뻑냄
	//----------------------------------------------------
	if (wcslen(_wszName) == 0)
		__debugbreak();

	//----------------------------------------------------
	// 2. 쿼리 준비
	//----------------------------------------------------
	PdhOpenQueryW(NULL, 0, &_hQuery);
	
	swprintf_s(querystring, MONITOR_PROCESS_NONPAGED_BYTES, _wszName);
	PdhAddCounterW(_hQuery, querystring, 0, &_hCounter_ProcessNonpagedBytes);

	swprintf_s(querystring, MONITOR_PROCESS_PRIVATE_BYTES, _wszName);
	PdhAddCounterW(_hQuery, querystring, 0, &_hCounter_ProcessPrivateBytes);

	Update();
}

CMonitorProcess::~CMonitorProcess()
{
	PdhRemoveCounter(_hCounter_ProcessNonpagedBytes);
	PdhRemoveCounter(_hCounter_ProcessPrivateBytes);
	PdhCloseQuery(_hQuery);
}

void CMonitorProcess::Update()
{
	//-----------------------------------------------------
	// [1] 지정된 프로세스의 사용률을 갱신한다.
	//-----------------------------------------------------
	ULARGE_INTEGER none;
	ULARGE_INTEGER nowTime;
	ULARGE_INTEGER kernel;
	ULARGE_INTEGER user;

	//-----------------------------------------------------
	// 0. 현재의 100ns 단위의 시간을 구한다. UTC 시간
	// 
	// < 프로세스 사용률 판단의 공식 >
	// . a = 샘플간격의 시스템 시간을 구함 (실제 지나간 시간)
	// . b = 프로세스의 cpu 사용 시간을 구함
	// . a : 100 = b : 사용률    공식으로 사용률을 구함
	//-----------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&nowTime);

	//-----------------------------------------------------
	// 1. 해당 프로세스가 사용한 시간을 구함
	// * 두번째, 세번째 인자는 실행, 종료 (미사용)
	//-----------------------------------------------------
	GetProcessTimes(_hProcess, (LPFILETIME)&none, (LPFILETIME)&none, (LPFILETIME)&kernel, (LPFILETIME)&user);

	//-----------------------------------------------------
	// 2. 이전에 저장된 프로세스 시간과의 차를 구해서 실제로
	//    얼마의 시간이 지났는지 확인
	// 
	//    그 후 실제 지나온 시간으로 나누면 사용률이 나옴
	//-----------------------------------------------------
	ULONGLONG timeDiff = nowTime.QuadPart - _ftProcess_LastTime.QuadPart;
	ULONGLONG userDiff = user.QuadPart - _ftProcess_LastUser.QuadPart;
	ULONGLONG kernelDiff = kernel.QuadPart - _ftProcess_LastKernel.QuadPart;

	ULONGLONG total = kernelDiff + userDiff;

	_fProcessTotal = (float)((double)total / (double)_iNumberOfProcessors / (double)timeDiff * 100.0f);
	_fProcessKernel = (float)((double)kernelDiff / (double)_iNumberOfProcessors / (double)timeDiff * 100.0f);
	_fProcessUser = (float)((double)userDiff / (double)_iNumberOfProcessors / (double)timeDiff * 100.0f);

	_ftProcess_LastTime = nowTime;
	_ftProcess_LastKernel = kernel;
	_ftProcess_LastUser = user;

	//-----------------------------------------------------
	// [2] PDH를 통해 얻어올 것을 얻는다. 
	//-----------------------------------------------------
	PdhCollectQueryData(_hQuery);

	PDH_FMT_COUNTERVALUE counterVal;
	PdhGetFormattedCounterValue(_hCounter_ProcessNonpagedBytes, PDH_FMT_LARGE, NULL, &counterVal);
	_lldProcessNonpagedBytes = counterVal.largeValue;
	PdhGetFormattedCounterValue(_hCounter_ProcessPrivateBytes, PDH_FMT_LARGE, NULL, &counterVal);
	_lldProcessPrivateBytes = counterVal.largeValue;
}

/////////////////////////////////////////////////////////////////////////////
// 시스템 모니터링 V1
/////////////////////////////////////////////////////////////////////////////

CMonitorSystem::CMonitorSystem() :
	_fProcessorTotal(0.0f), _fProcessorUser(0.0f), _fProcessorKernel(0.0f)
{
	//-----------------------------------------------------
	// 0. 값 초기화
	//-----------------------------------------------------
	_ftProcessor_LastKernel.QuadPart = 0;
	_ftProcessor_LastUser.QuadPart = 0;
	_ftProcessor_LastIdle.QuadPart = 0;

	//----------------------------------------------------
	// 1. 쿼리 준비
	//----------------------------------------------------
	PdhOpenQueryW(NULL, 0, &_hQuery);
	PdhAddCounterW(_hQuery, MONITOR_MEMORY_CACHE_FAULT_PER_SEC, 0, &_hCounter_CacheFault);
	PdhAddCounterW(_hQuery, MONITOR_MEMORY_COMMITED_BYTES, 0, &_hCounter_CommitBytes);
	PdhAddCounterW(_hQuery, MONITOR_MEMORY_NONPAGED_BYTES, 0, &_hCounter_NonpagedBytes);
	PdhAddCounterW(_hQuery, MONITOR_MEMORY_AVAILABLE_BYTES, 0, &_hCounter_AvailableBytes);
	PdhAddCounterW(_hQuery, MONITOR_NETWORK_SEGMENTS_RECVED, 0, &_hCounter_SegmentsReceved);
	PdhAddCounterW(_hQuery, MONITOR_NETWORK_SEGMENTS_SENT, 0, &_hCounter_SegmentsSent);
	PdhAddCounterW(_hQuery, MONITOR_NETWORK_SEGMENTS_RESENT, 0, &_hCounter_SegmentsReSent);

	//-----------------------------------------------------
	// 2. NIC 이름 리스트 가져오기
	// ** 필요한 것만 필터링
	//-----------------------------------------------------
	PDH_STATUS status;
	DWORD instanceLen = 0;
	DWORD size = 0;
	status = PdhEnumObjectItemsW(NULL, NULL, L"Network Adapter", NULL, &size, NULL, &instanceLen, PERF_DETAIL_WIZARD, 0);
	size = 0;
	std::vector<wchar_t> instanceBuf(instanceLen);
	status = PdhEnumObjectItemsW(NULL, NULL, L"Network Adapter", NULL, &size, instanceBuf.data(), &instanceLen, PERF_DETAIL_WIZARD, 0);
	
	// 필터링

	for (const wchar_t* p = instanceBuf.data(); *p != L'\0'; )
	{
		if(wcsncmp(p, L"Microsoft", 9) != 0
			&& wcsncmp(p, L"Teredo", 6) != 0
			&& wcsncmp(p, L"6to4", 4) != 0
			&& wcsncmp(p, L"Bluetooth", 9) != 0
			&& wcsncmp(p, L"WAN", 3) != 0
			&& wcsncmp(p, L"로컬", 2) != 0)
		{
			_vwstr_NIC_Names.emplace_back(p);
		}
		p += wcslen(p) + 1;
	}

	// for (int i = 0; i < _vwstr_NIC_Names.size(); i++)
	// 	wprintf_s(L"%s \n", _vwstr_NIC_Names[i].c_str());

	// 쿼리 준비

	std::vector<std::wstring>& NicNames = _vwstr_NIC_Names;
	int nicCnt = (int)NicNames.size();
	wchar_t wszrecvedformat[1024];
	wchar_t wszsentformat[1024];
	std::vector<PDH_HCOUNTER>& hCounter_NIC_BytesRecved = _hCounter_NIC_BytesRecved;
	std::vector<PDH_HCOUNTER>& hCounter_NIC_BytesSent = _hCounter_NIC_BytesSent;

	for (int i = 0; i < nicCnt; i++)
	{
		swprintf_s(wszrecvedformat, 512, MONITOR_NETWORK_BYTES_RECVED_FORMAT, NicNames[i].c_str());
		hCounter_NIC_BytesRecved.emplace_back();
		PdhAddCounterW(_hQuery, wszrecvedformat, 0, &hCounter_NIC_BytesRecved.back());

		swprintf_s(wszsentformat, 512, MONITOR_NETWORK_BYTES_SENT_FORMAT, NicNames[i].c_str());
		hCounter_NIC_BytesSent.emplace_back();
		PdhAddCounterW(_hQuery, wszsentformat, 0, &hCounter_NIC_BytesSent.back());
	}
	
	_lld_NIC_BytesRecved.resize(nicCnt);
	_lld_NIC_BytesSent.resize(nicCnt);

	Update();
}

CMonitorSystem::~CMonitorSystem()
{
	PdhRemoveCounter(_hCounter_CacheFault);
	PdhRemoveCounter(_hCounter_CommitBytes);
	PdhRemoveCounter(_hCounter_NonpagedBytes);
	PdhRemoveCounter(_hCounter_AvailableBytes);
	PdhRemoveCounter(_hCounter_SegmentsReceved);
	PdhRemoveCounter(_hCounter_SegmentsSent);
	PdhRemoveCounter(_hCounter_SegmentsReSent);

	while (_hCounter_NIC_BytesRecved.empty() == false)
	{
		PdhRemoveCounter(_hCounter_NIC_BytesRecved.back());
		_hCounter_NIC_BytesRecved.pop_back();
		PdhRemoveCounter(_hCounter_NIC_BytesSent.back());
		_hCounter_NIC_BytesSent.pop_back();
	}

	PdhCloseQuery(_hQuery);
}

void CMonitorSystem::Update()
{
	//-----------------------------------------------------
	// [1] 프로세서 사용률을 갱신한다.
	// 
	// 100ns단위의 정교한 시간 측정을 위해 FILETIME을 사용.
	// => ULARGE_INTEGER가 같은구조라 이거 사용
	// 
	// FILETIME 100ns 시간단위를 표기하는 구조체
	//-----------------------------------------------------
	ULARGE_INTEGER idle;
	ULARGE_INTEGER kernel;
	ULARGE_INTEGER user;

	//-----------------------------------------------------
	// 0. 시스템 사용시간을 구한다.
	// 
	// 아이들타임 / 커널타임(idle포함) / 유저 사용타임
	//-----------------------------------------------------
	if (GetSystemTimes((FILETIME*)&idle, (FILETIME*)&kernel, (FILETIME*)&user) == FALSE)
	{
		return;
	}

	//-----------------------------------------------------
	// 1. 커널 타임에는 idle타임이 포함되어있음 주의
	//-----------------------------------------------------
	ULONGLONG kernelDiff = kernel.QuadPart - _ftProcessor_LastKernel.QuadPart;
	ULONGLONG userDiff = user.QuadPart - _ftProcessor_LastUser.QuadPart;
	ULONGLONG idleDiff = idle.QuadPart - _ftProcessor_LastIdle.QuadPart;

	//-----------------------------------------------------
	// 2. 계산한 값을 바탕으로 값을 갱신한다.
	//-----------------------------------------------------
	ULONGLONG total = kernelDiff + userDiff;

	if (total == 0)
	{
		_fProcessorTotal = 0.0f;
		_fProcessorKernel = 0.0f;
		_fProcessorUser = 0.0f;
	}
	else
	{
		_fProcessorTotal = (float)((double)(total - idleDiff) / (double)total * 100.0);
		_fProcessorKernel = (float)((double)(kernelDiff - idleDiff) / (double)total * 100.0);
		_fProcessorUser = (float)((double)userDiff / (double)total * 100.0);
	}

	_ftProcessor_LastKernel = kernel;
	_ftProcessor_LastUser = user;
	_ftProcessor_LastIdle = idle;

	//-----------------------------------------------------
	// [2] 메모리 관련 얻어오기
	//-----------------------------------------------------
	PdhCollectQueryData(_hQuery);

	PDH_FMT_COUNTERVALUE counterVal;

	PdhGetFormattedCounterValue(_hCounter_CacheFault, PDH_FMT_DOUBLE, NULL, &counterVal);
	_fCacheFault = (float)counterVal.doubleValue;

	PdhGetFormattedCounterValue(_hCounter_CommitBytes, PDH_FMT_LARGE, NULL, &counterVal);
	_lldCommitBytes = counterVal.largeValue;

	PdhGetFormattedCounterValue(_hCounter_NonpagedBytes, PDH_FMT_LARGE, NULL, &counterVal);
	_lldNonpagedBytes = counterVal.largeValue;

	PdhGetFormattedCounterValue(_hCounter_AvailableBytes, PDH_FMT_LARGE, NULL, &counterVal);
	_AvailableBytes = counterVal.largeValue;

	PdhGetFormattedCounterValue(_hCounter_SegmentsReceved, PDH_FMT_DOUBLE, NULL, &counterVal);
	_fSegmentsRecved = (float)counterVal.doubleValue;

	PdhGetFormattedCounterValue(_hCounter_SegmentsSent, PDH_FMT_DOUBLE, NULL, &counterVal);
	_fSegmentsSent = (float)counterVal.doubleValue;

	PdhGetFormattedCounterValue(_hCounter_SegmentsReSent, PDH_FMT_DOUBLE, NULL, &counterVal);
	_fSegmentsResent = (float)counterVal.doubleValue;

	//------ 추가 --------
	// Network Bytes Recvd/Sent 획득
	int NICcnt = (int)_vwstr_NIC_Names.size();
	long long bytesRecved = 0;
	long long bytesSent = 0;
	for (int i = 0; i < NICcnt; i++)
	{
		PdhGetFormattedCounterValue(_hCounter_NIC_BytesRecved[i], PDH_FMT_LARGE, NULL, &counterVal);
		_lld_NIC_BytesRecved[i] = counterVal.largeValue;
		bytesRecved += counterVal.largeValue;
		
		PdhGetFormattedCounterValue(_hCounter_NIC_BytesSent[i], PDH_FMT_LARGE, NULL, &counterVal);
		_lld_NIC_BytesSent[i] = counterVal.largeValue;
		bytesSent += counterVal.largeValue;
	}
	_lldTotalBytesRecved = bytesRecved;
	_lldTotalBytesSent = bytesSent;
}






