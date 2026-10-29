#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <Windows.h>
#include <winternl.h>
#include <TlHelp32.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")


typedef NTSTATUS (NTAPI *NtReadVirtualMemory_t)(
	_In_ HANDLE ProcessHandle,
	_In_opt_ PVOID BaseAddress,
	_Out_writes_bytes_to_(NumberOfBytesToRead, *NumberOfBytesRead) PVOID Buffer,
	_In_ SIZE_T NumberOfBytesToRead,
	_Out_opt_ PSIZE_T NumberOfBytesRead
);

typedef NTSTATUS (NTAPI *NtOpenProcess_t)(
	_Out_ PHANDLE ProcessHandle,
	_In_ ACCESS_MASK DesiredAccess,
	_In_ OBJECT_ATTRIBUTES* ObjectAttributes,
	_In_opt_ CLIENT_ID* ClientId
);

NtReadVirtualMemory_t nt_read = nullptr;
NtOpenProcess_t nt_open = nullptr;

struct module_info {
	uint64_t start;
	uint64_t end;
	uint64_t size;
	std::string name;
};

uint32_t get_syscall_number(std::string function) {
	auto ntDll = GetModuleHandleA("ntdll.dll");
	auto fn = GetProcAddress(ntDll, function.c_str());
	auto syscall = *(uint32_t*)((uint64_t)fn + 0x4);
	return syscall;
}


void* get_syscall_stub(std::string function) {
	auto syscallNumber = get_syscall_number(function);
	uint8_t stub[] = { 0x49, 0x89, 0xCA, 0xB8, 0xFF, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
	stub[4] = syscallNumber;
	auto allocMem = VirtualAlloc(NULL, sizeof(stub), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	memcpy(allocMem, stub, sizeof(stub));
	return allocMem;
}

std::string get_module_name(char* path) {
	std::vector<std::string> parts;
	std::stringstream ss(path);
	std::string part;

	while (std::getline(ss, part, '\\')) {
		parts.push_back(part);
	}

	return parts.back();
}

module_info get_module_info(HANDLE handle, HMODULE mod, char* path) {
	module_info modinfo;
	modinfo.start = (uint64_t)mod;
	MODULEINFO memInfo;
	GetModuleInformation(handle, mod, &memInfo, sizeof(memInfo));
	modinfo.size = (uint64_t)memInfo.SizeOfImage;
	modinfo.end = (uint64_t)mod + (uint64_t)memInfo.SizeOfImage;
	modinfo.name = get_module_name(path);
	return modinfo;
}

uint32_t get_pid_by_name(std::string name) {
	auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	PROCESSENTRY32 pe;
	if (Process32First(snapshot, &pe)) {
		do {
			if (strcmp(pe.szExeFile, name.c_str()) == 0) {
				return pe.th32ProcessID;
			}
		} while (Process32Next(snapshot, &pe));
	}
	return 0;
}

int main(int argc, char* argv[]) {
	nt_read = (NtReadVirtualMemory_t)get_syscall_stub("NtReadVirtualMemory");
	nt_open = (NtOpenProcess_t)get_syscall_stub("NtOpenProcess");
	auto pid = get_pid_by_name(argv[1]);
	OBJECT_ATTRIBUTES objAttr;
	InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);
	CLIENT_ID cid;
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
	cid.UniqueThread = nullptr;
	HANDLE handle;
	nt_open(&handle, PROCESS_ALL_ACCESS, &objAttr, &cid);
	HMODULE hMods[1024];
	EnumProcessModulesEx(handle, hMods, sizeof(hMods), nullptr, LIST_MODULES_ALL);
	for (auto& mod : hMods) {
		char szModName[MAX_PATH];
		if (!GetModuleFileNameEx(handle, mod, szModName, sizeof(szModName))) {
			continue;
		}
		auto info = get_module_info(handle, mod, szModName);
		std::vector<uint8_t> buf(info.size);
		nt_read(handle, (void*)(info.start), buf.data(), info.size, nullptr);
		std::ofstream file(info.name, std::ios::binary);
		file.write((const char*)buf.data(), buf.size());
		file.close();
	}
}