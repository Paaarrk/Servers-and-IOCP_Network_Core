# Servers-and-IOCP_Network_Core
IOCP기반 네트워크 코어 <br>
 -> _Interlocked계열 원자 연산을 활용한 세션 핸들링 <br>
 -> lock-free 자료구조 사용 (queue, stack, tls memory pool) <br>
 -> 각 서버는 일주일간 안정적으로 동작 [[서버 안정성 테스트 결과]](./server_7days_result/result.md)


# Core
자료 구조들을 모아둔 라이브러리 <br>
  (락프리 큐, 스택, TLS 객체 풀, 프로파일러, 타이머, 로거)
### 대표 구조
< 락프리 큐 > <br>
[Core/Core/include/LockFreeQueue.hpp](./Core/Core/include/LockFreeQueue.hpp)

< 락프리 스택 > <br>
[Core/Core/include/LockFreeStack.hpp](./Core/Core/include/LockFreeStack.hpp)

< TLS 메모리 풀 > <br>
[Core/Core/include/TlsObjectPool_IntrusiveList.hpp](./Core/Core/include/TLSObjectPool_IntrusiveList.hpp)


# NetLibrary
IOCP 넷코어 라이브러리 <br>
  (네트워크 서버, 클라이언트, 세션, 참조 기반 직렬화 버퍼, 컨텐츠 단위(Zone))
### 대표 구조
< 참조 기반 직렬화 버퍼 > <br>
[NetLibrary/NetLibrary/include/TlsPacket.hpp](./NetLibrary/NetLibrary/include/TlsPacket.hpp)

< 서버 > <br>
[NetLibrary/NetLibrary/include/ZoneServer.h](./NetLibrary/NetLibrary/include/ZoneServer.h) <br>
[NetLibrary/NetLibrary/src/ZoneServer.cpp](./NetLibrary/NetLibrary/src/ZoneServer.cpp)

< 세션 > <br>
[NetLibrary/NetLibrary/include/ZoneSession.h](./NetLibrary/NetLibrary/include/ZoneSession.h) <br>
[NetLibrary/NetLibrary/src/ZoneSession.cpp](./NetLibrary/NetLibrary/src/ZoneSession.cpp)

< 컨텐츠 단위 (Zone) > <br>
[NetLibrary/NetLibrary/include/ZoneManager.h](./NetLibrary/NetLibrary/include/ZoneManager.h) <br>
[NetLibrary/NetLibrary/src/ZoneManager.cpp](./NetLibrary/NetLibrary/src/ZoneManager.cpp) <br>
[NetLibrary/NetLibrary/include/Zone.h](./NetLibrary/NetLibrary/include/Zone.h) <br>
[NetLibrary/NetLibrary/src/Zone.cpp](./NetLibrary/NetLibrary/src/Zone.cpp) <br>
[NetLibrary/NetLibrary/include/ZoneType.h](./NetLibrary/NetLibrary/include/ZoneType.h)

## 특징
* 세션
  - 세션 참조 카운트 기반 생명주기 관리
  - 센드큐는 락프리 큐 사용
* 네트워크 코어: 
  - IOCP기반
  - 세션 배열 및 인덱스 관리(락프리 스택)
* 컨텐츠 단위 Zone (Tick 기반):
  - IOCP Worker 혹은 고정 스레드중 어디서 동작할 지 선택
  - Zone 참조 카운트 기반 생명주기 관리
  - ZoneManager가 Zone 배열 및 인덱스 관리(락프리 스택)
  - 사용자는 ZoneId로 Zone활용
  - 틱 조절을 위한 타이머(예약) 스레드 존재
  


# Server
- ChatServer_Multi : Core과 NetLibrary를 활용해 만든 채팅 서버
- LoginServer : Core과 NetLibrary를 활용해 만든 로그인 서버
- MonitoringServer: Core, NetLibrary 분리 전 상태로 만든 모니터링 서버
- ZoneServerEchoTest: 게임 서버를 위한 Zone기능을 추가하고 기능 테스트를 위해 만든 서버

# Requirements
- Visual Studio 2022
- Redis
- MySQL 8.0 <br>
(각 서버 include 내부에 lib, dll있음)

# Builds

```
Release O0 (inline만 허용(O1)) / C++17 
Server  /
    ├ Config     /Config.cnf                 <모두 필요>
    ├ Login      /LoginServer.exe            <채팅의 로그인 서버>
    ├ Chat       /ChatServer.exe             <채팅 서버>
    ├ Monitor    /MonitorServer.exe          <종합 모니터링 서버>
    └ ZoneEcho   /ZoneEchoServerTest.exe     <Zone 기능 테스트용 에코 서버>

```
