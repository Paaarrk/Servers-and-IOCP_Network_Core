#ifndef __MONITOR_CLASS_H__
#define __MONITOR_CLASS_H__
#include <windows.h>
#include <pdh.h>
#include <vector>
#include <string>

namespace Core
{
	/////////////////////////////////////////////////////////////////////////////
	// 프로세스 모니터링 V1
	/////////////////////////////////////////////////////////////////////////////
	class CMonitorProcess
	{
	public:
		//-----------------------------------------------------------------------
		// info: 생성자
		// parameter: 프로세스 핸들 (미입력시 자기 자신)
		// return: -
		//-----------------------------------------------------------------------
		CMonitorProcess(HANDLE hProcess = INVALID_HANDLE_VALUE);

		~CMonitorProcess();

		//-----------------------------------------------------------------------
		// info: 업데이트 (500ms ~ 1000ms 주기 업데이트 필요
		// parameter: -
		//-----------------------------------------------------------------------
		void Update();

		const wchar_t* GetInstanceName(void) { return _wszName; }

		float ProcessTotal(void) const { return _fProcessTotal; }
		float ProcessUser(void) const { return _fProcessUser; }
		float ProcessKernel(void) const { return _fProcessKernel; }

		long long ProcessNonpagedBytes(void) const { return _lldProcessNonpagedBytes; }
		long long ProcessPrivateBytes(void) const { return _lldProcessPrivateBytes; }

	private:
		HANDLE	_hProcess;
		int		_iNumberOfProcessors;
		wchar_t _wszName[128];

		//-- CPUS --//

		float	_fProcessTotal;
		float	_fProcessUser;
		float	_fProcessKernel;

		ULARGE_INTEGER	_ftProcess_LastKernel;
		ULARGE_INTEGER	_ftProcess_LastUser;
		ULARGE_INTEGER	_ftProcess_LastTime;

		//-- PROCESS --//

		long long _lldProcessNonpagedBytes;
		long long _lldProcessPrivateBytes;

		//-- Query --//
		PDH_HQUERY _hQuery;
		PDH_HCOUNTER _hCounter_ProcessNonpagedBytes;
		PDH_HCOUNTER _hCounter_ProcessPrivateBytes;

		CMonitorProcess(const CMonitorProcess&) = delete;
		CMonitorProcess& operator=(const CMonitorProcess&) = delete;
	};

	/////////////////////////////////////////////////////////////////////////////
	// 시스템 모니터링 V1
	/////////////////////////////////////////////////////////////////////////////
	class CMonitorSystem
	{
	public:
		//-----------------------------------------------------------------------
		// info: 생성자
		// parameter: 프로세스 핸들 (미입력시 자기 자신)
		// return: -
		//-----------------------------------------------------------------------
		CMonitorSystem();

		~CMonitorSystem();

		//-----------------------------------------------------------------------
		// info: 업데이트 (500ms ~ 1000ms 주기 업데이트 필요
		// parameter: -
		//-----------------------------------------------------------------------
		void Update();

		float ProcessorTotal(void) const { return _fProcessorTotal; }
		float ProcessorUser(void) const { return _fProcessorUser; }
		float ProcessorKernel(void) const { return _fProcessorKernel; }

		float SystemCacheFault(void) const { return _fCacheFault; }
		long long SystemCommitBytes(void) const { return _lldCommitBytes; }
		long long SystemNonpagedBytes(void) const { return _lldNonpagedBytes; }
		long long SystemAvailableBytes(void) const { return _AvailableBytes; }
		float SystemSegmentRecved(void) const { return _fSegmentsRecved; }
		float SystemSegmentSent(void) const { return _fSegmentsSent; }
		float SystemSegmentRetransmitted(void) const { return _fSegmentsResent; }

		const std::vector<std::wstring>& GetNICnames(void) const { return _vwstr_NIC_Names; }
		long long SystemTotalNetworkBytesRecved(void) const { return _lldTotalBytesRecved; }
		long long SystemTotalNetworkBytesSent(void) const { return _lldTotalBytesSent; }

		const std::vector<long long>& SystemNetworkBytesRecved(void) const { return _lld_NIC_BytesRecved; }
		const std::vector<long long>& SystemNetworkBytesSent(void) const { return _lld_NIC_BytesSent; }
	private:
		//-- CPUS --//

		float	_fProcessorTotal;
		float	_fProcessorUser;
		float	_fProcessorKernel;

		ULARGE_INTEGER	_ftProcessor_LastKernel;
		ULARGE_INTEGER	_ftProcessor_LastUser;
		ULARGE_INTEGER	_ftProcessor_LastIdle;

		//-- MEMORY --//

		float _fCacheFault;
		long long _lldCommitBytes;
		long long _lldNonpagedBytes;
		long long _AvailableBytes;

		//-- NETWORK --//

		float _fSegmentsRecved;
		float _fSegmentsSent;
		float _fSegmentsResent;

		// 추가 -----------
		long long _lldTotalBytesRecved;
		long long _lldTotalBytesSent;
		std::vector<long long> _lld_NIC_BytesRecved;
		std::vector<long long> _lld_NIC_BytesSent;

		//-- Query --//
		PDH_HQUERY _hQuery;
		PDH_HCOUNTER _hCounter_CacheFault;
		PDH_HCOUNTER _hCounter_CommitBytes;
		PDH_HCOUNTER _hCounter_NonpagedBytes;
		PDH_HCOUNTER _hCounter_AvailableBytes;
		PDH_HCOUNTER _hCounter_SegmentsReceved;
		PDH_HCOUNTER _hCounter_SegmentsSent;
		PDH_HCOUNTER _hCounter_SegmentsReSent;

		// 추가 -----------

		std::vector<std::wstring> _vwstr_NIC_Names;
		std::vector<PDH_HCOUNTER> _hCounter_NIC_BytesRecved;
		std::vector<PDH_HCOUNTER> _hCounter_NIC_BytesSent;

		CMonitorSystem(const CMonitorSystem&) = delete;
		CMonitorSystem& operator=(const CMonitorSystem&) = delete;
	};
}

#endif