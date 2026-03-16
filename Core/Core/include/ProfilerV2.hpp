#ifndef __PROFILER_H__
#define __PROFILER_H__
#include <windows.h>
#include <time.h>
#include <stdio.h>
#include <algorithm>

namespace Core
{
	//////////////////////////////////////////////////////////////////////////
	// Profiler 
	// 멀티스레드에 적합한 프로파일러 입니다.
	//////////////////////////////////////////////////////////////////////////

//#define _PROFILE

#define PF_TAGS_CAPACITY	23

	class ProfileManager;
#ifdef _PROFILE
#define PRO_START()				ProfileManager::StartProfile()
#define	PRO_BEGIN(LtagName)		ProfileManager::ProfileBegin(LtagName)
#define PRO_END(LtagName)		ProfileManager::ProfileEnd(LtagName)
#define PRO_EXIT()				ProfileManager::ExitProfile()
#else
#define PRO_START()				
#define PRO_BEGIN(TagName)
#define PRO_END(TagName)				
#define PRO_EXIT()
#endif

	// 프로파일 매니저
	class ProfileManager
	{
	public:
		struct st_profile
		{
			st_profile* next;
			const wchar_t* name;
			__int64		liTotalTime;
			__int64		liStartTime;
			__int64		liMin[2];
			__int64		liMax[2];
			__int64		liCallCnt;
			st_profile(const wchar_t* name = nullptr) :name(name), liTotalTime(0), liStartTime(0),
				liCallCnt(0), next(nullptr)
			{
				liMin[0] = 0; liMin[1] = 0;
				liMax[0] = 0; liMax[1] = 0;
			}
		};
		static bool Write(DWORD tid)
		{
			FILE* file;

			wchar_t filename[64];
			tm tm;
			time_t mytime = time(NULL);
			localtime_s(&tm, &mytime);
			swprintf_s(filename, L"Profile_%04d%02d%02d_%02d%02d%02d.txt", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);

			AcquireSRWLockExclusive(&_fileWrite);
			_wfopen_s(&file, filename, L"ab");
			if (file == nullptr)
			{
				ReleaseSRWLockExclusive(&_fileWrite);
				return false;
			}
			if (_ftelli64(file) == 0)
				fputwc(0xFEFF, file);

			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);

			fwprintf_s(file, L"+------+-----------------------------------------+-------------------+-------------------+-------------------+-------------------+\n");
			fwprintf_s(file, L"|%5s |%40ls |%18s |%18s |%18s |%18s |\n", L"TID", L"Name", L"Average", L"Min", L"Max", L"Call");
			fwprintf_s(file, L"+------+-----------------------------------------+-------------------+-------------------+-------------------+-------------------+\n");
			double avg;
			double min;
			double max;
			st_profile* pProfile;
			for (int i = 0; i < PF_TAGS_CAPACITY; i++)
			{
				pProfile = _profiles[i];
				while (pProfile != nullptr)
				{
					if (pProfile->liCallCnt == 0)
					{
						avg = 0;
					}
					else if (pProfile->liCallCnt < 4)
						avg = (double)(pProfile->liTotalTime) / freq.QuadPart / (pProfile->liCallCnt) * 1000000;
					else
						avg = (double)(pProfile->liTotalTime - pProfile->liMin[0] - pProfile->liMin[1] - pProfile->liMax[0] - pProfile->liMax[1]) / freq.QuadPart / (pProfile->liCallCnt - 4) * 1000000;

					min = (double)pProfile->liMin[0] / freq.QuadPart * 1000000;
					max = (double)pProfile->liMax[1] / freq.QuadPart * 1000000;
					fwprintf_s(file, L"|%5u |%40ls |%16.4lf㎲ |%16.4lf㎲ |%16.4lf㎲ |%18lld |\n", tid, pProfile->name, avg, min, max, pProfile->liCallCnt);
					pProfile = pProfile->next;
				}
			}
			fwprintf_s(file, L"+------+-----------------------------------------+-------------------+-------------------+-------------------+-------------------+\n");
			fclose(file);


			ReleaseSRWLockExclusive(&_fileWrite);
			return true;
		}
		static void Clear()
		{
			st_profile* pProfile;
			for (int i = 0; i < PF_TAGS_CAPACITY; i++)
			{
				pProfile = _profiles[i];
				while (pProfile != nullptr)
				{
					pProfile->liTotalTime = 0;
					pProfile->liStartTime = 0;
					pProfile->liMin[0] = 0;
					pProfile->liMin[1] = 0;
					pProfile->liMax[0] = 0;
					pProfile->liMax[1] = 0;
					pProfile->liCallCnt = 0;
					pProfile = pProfile->next;
				}
			};
		}

		static void StartProfile()
		{
			_profiles = new st_profile * [PF_TAGS_CAPACITY];
			memset(_profiles, 0, sizeof(st_profile*) * PF_TAGS_CAPACITY);
		}

		static void ProfileEnd(const wchar_t* tag)
		{
			LARGE_INTEGER end;
			QueryPerformanceCounter(&end);
			st_profile* myProfile = GetProfile(tag);
			__int64 deltatime = end.QuadPart - myProfile->liStartTime;
			myProfile->liTotalTime += deltatime;

			__int64 temp[5];
			temp[0] = myProfile->liMin[0];
			temp[1] = myProfile->liMin[1];
			temp[2] = myProfile->liMax[0];
			temp[3] = myProfile->liMax[1];
			temp[4] = deltatime;
			std::sort(temp, temp + 5);
			if (myProfile->liCallCnt < 4)
			{
				myProfile->liMin[0] = temp[1];
				myProfile->liMin[1] = temp[2];
				myProfile->liMax[0] = temp[3];
				myProfile->liMax[1] = temp[4];
			}
			else
			{
				myProfile->liMin[0] = temp[0];
				myProfile->liMin[1] = temp[1];
				myProfile->liMax[0] = temp[3];
				myProfile->liMax[1] = temp[4];
			}

			myProfile->liCallCnt++;

			if (GetAsyncKeyState(VK_F1))
			{
				Write(GetCurrentThreadId());
			}
			else if (GetAsyncKeyState('R'))
			{
				Clear();
			}
		}
		static void ProfileBegin(const wchar_t* tag)
		{
			st_profile* myProfile = GetProfile(tag);
			LARGE_INTEGER start;
			QueryPerformanceCounter(&start);
			myProfile->liStartTime = start.QuadPart;
		}

		static void ExitProfile()
		{
			ClearProfiles();
			delete _profiles;
		}
	private:
		ProfileManager() = delete;
		static unsigned int hash(const wchar_t* wstr)
		{
			unsigned int hash = 5381;
			int i = 0;
			while (*wstr) {
				hash = ((hash << 5) + hash) + *wstr;
				wstr++;
				if (i++ == 20)
					break;
			}
			return hash % PF_TAGS_CAPACITY;
		}
		static st_profile* GetProfile(const wchar_t* tag)
		{
			if (tag == nullptr)
				return nullptr;
			st_profile* value = nullptr;
			unsigned int idx = hash(tag);
			st_profile* pProfile = _profiles[idx];
			if (pProfile == nullptr)
			{
				_profiles[idx] = new st_profile(tag);
				return _profiles[idx];
			}

			while (pProfile->next != nullptr)
			{
				if (wcscmp(pProfile->name, tag) == 0)
					return pProfile;
				else
					pProfile = pProfile->next;
			}
			if (pProfile->next == nullptr && wcscmp(pProfile->name, tag) != 0)
			{
				pProfile->next = new st_profile(tag);
				return pProfile->next;
			}
			else
				return pProfile;
		}
		static void ClearProfiles()
		{
			st_profile* pProfile;
			for (int i = 0; i < PF_TAGS_CAPACITY; i++)
			{
				pProfile = _profiles[i];
				while (pProfile != nullptr)
				{
					st_profile* pDelete = pProfile;
					pProfile = pProfile->next;
					delete pDelete;
				}
				_profiles[i] = nullptr;
			};
		}

		inline static thread_local st_profile** _profiles;
		inline static SRWLOCK _fileWrite = SRWLOCK_INIT;
	};

	// 편하게 프로파일링 하기위핸 래퍼클래스
	class Profile
	{
	public:
		Profile(const wchar_t* tag)
		{
			PRO_BEGIN(tag);
			_tag = tag;
		}
		~Profile()
		{
			PRO_END(_tag);
		}
	private:
		const wchar_t* _tag;
	};
}
#endif