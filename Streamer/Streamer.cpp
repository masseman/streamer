#include "stdafx.h"
#include <windows.h>
#include <winsock.h>
#include <cstring>
#include <string>
#include <iostream>

#define SERVICE_NAME _T("Network Sound Streamer")

namespace
{
  SERVICE_STATUS service_status = {0};
  SERVICE_STATUS_HANDLE status_handle = NULL;
  HANDLE service_stop_event = INVALID_HANDLE_VALUE;
  bool stopped = false;

  WAVEFORMATEX format;
  const char* wanted_device_name = 0;

  bool is_data_incoming = false;
  WAVEHDR* current_recorded_header = 0;
  HANDLE data_recorded_semaphore;

  void CALLBACK waveInProc(HWAVEIN hwi,
						   UINT uMsg,
                           DWORD_PTR dwInstance,
                           DWORD_PTR dwParam1,
                           DWORD_PTR dwParam2)
  {
    switch (uMsg)
    {
    case WIM_OPEN:
      break;

    case WIM_DATA:
      is_data_incoming = true;
      current_recorded_header = (WAVEHDR*) dwParam1;
      ReleaseSemaphore(data_recorded_semaphore, 1, NULL);
      break;

    case WIM_CLOSE:
      is_data_incoming = false;
      ReleaseSemaphore(data_recorded_semaphore, 1, NULL);
      break;
    }
  }

  bool allZeroes(LPSTR data, DWORD length)
  {
    for (UINT i = 0; i < length; i++)
    {
      if (data[i])
        return false;
    }
    return true;
  }

  WSADATA wsadata;
  SOCKADDR_IN target;
  SOCKET sock;
  bool connected = false;

  void dataRecorded(LPSTR data, DWORD length)
  {
    if (allZeroes(data, length))
	{
      Sleep(100);
      return;
	}

    if (!connected)
    {
      sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	  if (connect(sock, (SOCKADDR*) &target, sizeof(target)) == 0)
      {
        std::cout << "Connected to server" << std::endl;
        connected = true;
      }
	  else
	  {
        closesocket(sock);
	    connected = false;
		Sleep(2000);
	  }
    }
    if (connected)
    {
      while (length > 0)
      {
        int sent_bytes = send(sock, data, length, 0);
        if (sent_bytes == SOCKET_ERROR)
        {
          closesocket(sock);
          connected = false;
          break;
        }
        else
        {
          length -= sent_bytes;
          data += sent_bytes;
        }
      }
    }
  }

  bool equals(const TCHAR* s1, const char* s2)
  {
    while (*s1 && *s2)
	{
      if (*s1 != *s2)
        return false;
      ++s1;
      ++s2;
	}
	return !*s1 && !*s2;
  }
}

DWORD WINAPI innerMain(LPVOID lpParam = 0)
{
  UINT device_id = 0xFFFF;
  std::wstring device_name;
  {
    UINT num = waveInGetNumDevs();
    for (UINT i = 0; i < num; i++)
    {
	  WAVEINCAPS waveInCap;
      if (waveInGetDevCaps(i,
                           &waveInCap,
                           sizeof(WAVEINCAPS)) == MMSYSERR_NOERROR)
      {
        TCHAR* name = waveInCap.szPname;
        if ((wanted_device_name &&
			 equals(name, wanted_device_name)) ||
			(!wanted_device_name &&
              (device_id == 0xFFFF ||
			   _tcsstr(name, _T("Virtual Cable 1")) ||
               _tcsstr(name, _T("Stereo Mix")))))
        {
          device_id = i;
		  device_name = name;
        }
      }
    }
  }

  if (device_id != 0xFFFF)
  {
	std::wcout << "Using device " << device_name << std::endl;

    format.nBlockAlign = (format.wBitsPerSample * format.nChannels) >> 3;
    format.nAvgBytesPerSec = format.nBlockAlign * format.nSamplesPerSec;

    const int buffer_count = 8;
    const int buffer_size = 4096;

    HWAVEIN handle;
    if (waveInOpen(&handle,
                   device_id,
                   &format,
                   (DWORD_PTR) &waveInProc,
                   (DWORD_PTR) &handle,
				   CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
    {
      WAVEHDR headers[buffer_count];
      for (int i = 0; i < buffer_count; i++)
      {
        headers[i].dwLoops = 0;
        headers[i].dwUser = 0;
        headers[i].lpNext = 0;
        headers[i].reserved = 0;
        headers[i].lpData = (LPSTR) malloc(buffer_size);
        headers[i].dwBufferLength = buffer_size;
        headers[i].dwBytesRecorded = 0;
        headers[i].dwFlags = 0;

        if (waveInPrepareHeader(handle,
                                &headers[i],
                                sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        {
          if (i == 0)
          {
            waveInAddBuffer(handle,
                            &headers[i],
                            sizeof(WAVEHDR));
          }
        }
      }

      if (waveInStart(handle) == MMSYSERR_NOERROR)
      {
        while (!stopped)
        {
          WaitForSingleObject(data_recorded_semaphore, INFINITE);

          if (!stopped)
          {
            if (current_recorded_header &&
                current_recorded_header->dwBytesRecorded > 0)
            {
              if (is_data_incoming)
              {
                dataRecorded(current_recorded_header->lpData,
                             current_recorded_header->dwBytesRecorded);
              }

              for (int i = 0; i < buffer_count; i++)
              {
                if ((headers[i].dwFlags & WHDR_INQUEUE) == 0)
                {
                  waveInAddBuffer(handle,
                                  &headers[i],
                                  sizeof(WAVEHDR));
                }
              }
			}
		  }
		}

      }
    }
  }
  else
  {
    std::cerr << "No appropriate device found" << std::endl;
  }

  CloseHandle(data_recorded_semaphore);

  closesocket(sock);

  return ERROR_SUCCESS;
}

VOID WINAPI ServiceCtrlHandler(DWORD control_code)
{
  if ((control_code == SERVICE_CONTROL_STOP ||
       control_code == SERVICE_CONTROL_SHUTDOWN) &&
      service_status.dwCurrentState == SERVICE_RUNNING)
  {
    service_status.dwControlsAccepted = 0;
    service_status.dwCurrentState = SERVICE_STOP_PENDING;
    service_status.dwWin32ExitCode = 0;
    service_status.dwCheckPoint = 4;
    SetServiceStatus(status_handle, &service_status);

    stopped = true;
    ReleaseSemaphore(data_recorded_semaphore, 1, NULL);

	SetEvent(service_stop_event);
  }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
  status_handle =
    RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

  ZeroMemory(&service_status, sizeof (service_status));

  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status.dwControlsAccepted = 0;
  service_status.dwCurrentState = SERVICE_START_PENDING;
  service_status.dwWin32ExitCode = 0;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwCheckPoint = 0;
  SetServiceStatus(status_handle, &service_status);

  service_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!service_stop_event) 
  {   
    service_status.dwControlsAccepted = 0;
    service_status.dwCurrentState = SERVICE_STOPPED;
    service_status.dwWin32ExitCode = GetLastError();
    service_status.dwCheckPoint = 1;
    SetServiceStatus(status_handle, &service_status);
    return;
  }

  service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  service_status.dwCurrentState = SERVICE_RUNNING;
  service_status.dwWin32ExitCode = 0;
  service_status.dwCheckPoint = 0;
  SetServiceStatus(status_handle, &service_status);

  HANDLE hThread = CreateThread(NULL, 0, innerMain, NULL, 0, NULL);

  WaitForSingleObject(hThread, INFINITE);

  CloseHandle(service_stop_event);

  service_status.dwControlsAccepted = 0;
  service_status.dwCurrentState = SERVICE_STOPPED;
  service_status.dwWin32ExitCode = 0;
  service_status.dwCheckPoint = 3;
  SetServiceStatus(status_handle, &service_status);
}

int main(int argc, char* argv[])
{
  WSAStartup(0x0202, &wsadata);

  target.sin_family = AF_INET;
  target.sin_addr.s_addr = inet_addr("192.168.1.22");
  target.sin_port = htons(1234);

  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 44100;
  format.wBitsPerSample = 16;

  data_recorded_semaphore =
    CreateSemaphore(NULL,
                    10,
                    10,
                    NULL);
	
  bool run_as_service = true;
  bool show_usage = false;
  for (int i = 1; i < argc; i++)
  {
	if (strstr(argv[i], "/N"))
	{
      run_as_service = false;
	}
	else if (strstr(argv[i], "/D"))
	{
      wanted_device_name = argv[++i];
	}
	else if (strstr(argv[i], "/H"))
	{
	  if (i < argc - 1)
	  {
		hostent* hp = gethostbyname(argv[++i]);
		if (hp)
		{
          target.sin_addr.s_addr =
			inet_addr(inet_ntoa(*(in_addr*) hp->h_addr_list[0]));
		}
		else
		{
		  std::cerr << "Host lookup failed" << std::endl;
		  return -1;
		}
	  }
	  else
	    show_usage = true;
	}
	else if (strstr(argv[i], "/P"))
	{
	  if (i < argc - 1)
        target.sin_port = htons(atoi(argv[++i]));
	  else
		show_usage = true;
	}
	else if (strstr(argv[i], "/C"))
	{
	  if (i < argc - 1)
        format.nChannels = atoi(argv[++i]);
	  else
		show_usage = true;
	}
	else if (strstr(argv[i], "/S"))
	{
	  if (i < argc - 1)
        format.nSamplesPerSec = atoi(argv[++i]);
	  else
		show_usage = true;
    }
	else if (strstr(argv[i], "/B"))
	{
	  if (i < argc - 1)
        format.wBitsPerSample = atoi(argv[++i]);
 	  else
		show_usage = true;
    }
	else if (strstr(argv[i], "/?"))
	{
      show_usage = true;
	}
  }

  if (show_usage)
  {
    std::cout
      << "Usage: " << argv[0]
      << " [/N]"
	  << " [/D <Recording device name>]"
	  << " [/H <Destination host>]"
	  << " [/P <Destination port>]"
	  << " [/C <Number of channels>]"
	  << " [/S <Sample rate>]"
	  << " [/B <Number of bits>]"
	  << std::endl
	  << std::endl
	  << "Use /N to not run as a service."
	  << std::endl;
    return 0;
  }

  if (run_as_service)
  {
    SERVICE_TABLE_ENTRY service_table[] =
    {
      {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
      {NULL, NULL}
    };
 
    if (StartServiceCtrlDispatcher(service_table) == FALSE)
      return GetLastError();
  }
  else
  {
    innerMain();
  }

  return 0;
}