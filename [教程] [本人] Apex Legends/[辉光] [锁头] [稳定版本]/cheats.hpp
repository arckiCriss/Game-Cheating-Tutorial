#pragma once

#include "entity.hpp"

constexpr const int NUM_ENT_ENTRIES = 0x10000;
constexpr const int MAX_PLAYERS = 100;

client_info g_clients[NUM_ENT_ENTRIES];		// 客户端信息

class apex_cheats
{
private:
	rm_driver m_driver;		// 读写
	HWND m_hwnd;			// 游戏窗口

	entity m_local;				// 本地玩家

	entity m_players[MAX_PLAYERS];					// 玩家列表

public:
	apex_cheats() {}
	~apex_cheats() {}

	/* 初始化 */
	bool initialize()
	{
		// 初始化驱动相关
		bool state = m_driver.initialize(L"r5apex.exe");
		if (state == false) return false;

		// 查找游戏窗口
		m_hwnd = FindWindowA("Respawn001", "Apex Legends");
		if (m_hwnd == false) return false;

		IMAGE_DOS_HEADER dos = m_driver.read<IMAGE_DOS_HEADER>(m_driver.m_base);
		std::cout << "[+] DOS文件头偏移 : 0x" << std::hex << dos.e_lfanew << std::endl;

		IMAGE_NT_HEADERS64 nt64 = m_driver.read<IMAGE_NT_HEADERS64>(m_driver.m_base + dos.e_lfanew);
		std::cout << "[+] 时间戳 : 0x" << std::hex << nt64.FileHeader.TimeDateStamp << std::endl;
		if (apex_offsets::TimeDateStamp != nt64.FileHeader.TimeDateStamp)
		{
			std::cout << "[-] 时间戳不相同,请更新偏移" << std::endl;
			return false;
		}

		std::cout << "[+] 校验和 : 0x" << std::hex << nt64.OptionalHeader.CheckSum << std::endl;
		if (apex_offsets::CheckSum != nt64.OptionalHeader.CheckSum)
		{
			std::cout << "[-] 校验和不相同,请更新偏移" << std::endl;
			return false;
		}

		return true;
	}

	/* 获取有效玩家 */
	int get_visiable_player()
	{
		// 玩家数量
		int num = 0;

		// 清空玩家
		for (int i = 0; i < MAX_PLAYERS; i++) m_players[i].update(nullptr, 0);

		// 找出真正的玩家地址
		for (int i = 0; i < NUM_ENT_ENTRIES; i++)
		{
			// 先拿到实例地址
			DWORD64 addr = g_clients[i].pEntity;

			// 地址为空
			if (addr == 0) continue;

			// 地址有误
			if ((addr & 0x07) != 0 || addr >= (1ULL << 48)) continue;

			// 如果是自己
			if (addr == m_local.m_base) continue;

			entity e(&m_driver, addr);

			// 如果是玩家或者电脑人
			if (e.is_player() || e.is_npc())
			{
				// 没有死亡
				if (e.get_current_health() > 0 && e.is_life())
				{
					// 加入玩家列表
					m_players[num++] = e;
				}
			}

			/*
			// 类型判断
			DWORD64 client_networkable_vtable = m_driver.read<DWORD64>(addr + 8 * 3);
			if (client_networkable_vtable == 0) continue;

			DWORD64 get_client_class = m_driver.read<DWORD64>(client_networkable_vtable + 8 * 3);
			if (get_client_class == 0) continue;

			DWORD32 disp = m_driver.read<DWORD32>(get_client_class + 3);
			if (disp == 0) continue;

			DWORD64 client_class_ptr = get_client_class + disp + 7;
			client_class_info info = m_driver.read<client_class_info>(client_class_ptr);
			char* names = m_driver.read_char_array(info.pNetworkName, 128);

			// 如果是玩家或者电脑人
			if (strcmp(names, "CPlayer") == 0 || strcmp(names, "CAI_BaseNPC") == 0)
			{
				// 不是是自己
				if (addr != m_local.m_base)
					m_players[num++].update(&m_driver, addr);
			}
			delete[] names;
			*/
		}

		return num;
	}

	/* 角度计算 */
	void calc_angle(Vec3& vecOrigin, Vec3& vecOther, Vec3& vecAngles)
	{
		Vec3 vecDelta = Vec3{ (vecOrigin[0] - vecOther[0]), (vecOrigin[1] - vecOther[1]), (vecOrigin[2] - vecOther[2]) };
		float hyp = sqrtf(vecDelta[0] * vecDelta[0] + vecDelta[1] * vecDelta[1]);

		vecAngles[0] = (float)atan(vecDelta[2] / hyp)		*(float)(180.f / 3.14159265358979323846);
		vecAngles[1] = (float)atan(vecDelta[1] / vecDelta[0])	*(float)(180.f / 3.14159265358979323846);
		vecAngles[2] = (float)0.f;

		if (vecDelta[0] >= 0.f) vecAngles[1] += 180.0f;
	}

	/* 构造一个向量 */
	void make_vector(Vec3& vecAngle, Vec3& out)
	{
		float pitch = float(vecAngle[0] * 3.14159265358979323846 / 180);
		float tmp = float(cos(pitch));
		float yaw = float(vecAngle[1] * 3.14159265358979323846 / 180);
		out[0] = float(-tmp * -cos(yaw));
		out[1] = float(sin(yaw)*tmp);
		out[2] = float(-sin(pitch));
	}

	/* 获取最大的fov */
	float get_max_fov(Vec3& vecAngle, Vec3& vecOrigin, Vec3& vecOther)
	{
		Vec3 ang, aim;
		double fov = 0.0;

		calc_angle(vecOrigin, vecOther, ang);
		make_vector(vecAngle, aim);
		make_vector(ang, ang);

		float mag_s = sqrt((aim[0] * aim[0]) + (aim[1] * aim[1]) + (aim[2] * aim[2]));
		float mag_d = sqrt((aim[0] * aim[0]) + (aim[1] * aim[1]) + (aim[2] * aim[2]));

		float u_dot_v = aim[0] * ang[0] + aim[1] * ang[1] + aim[2] * ang[2];
		fov = acos(u_dot_v / (mag_s*mag_d)) * (180.f / 3.14159265358979323846);
		fov *= 1.4;

		if (isnan(fov)) return 0.0f;
		return float(fov);
	}

	/* 计算自瞄角度 */
	Vec3 get_aimbot_angle(Vec3& vecOrigin, Vec3& vecOther)
	{
		Vec3 vecAngles{};
		Vec3 vecDelta = Vec3{ (vecOrigin[0] - vecOther[0]), (vecOrigin[1] - vecOther[1]), (vecOrigin[2] - vecOther[2]) };
		float hyp = sqrtf(vecDelta[0] * vecDelta[0] + vecDelta[1] * vecDelta[1]);

		float  M_PI = 3.14159265358979323846f;
		vecAngles[0] = (float)atan(vecDelta[2] / hyp)		*(float)(180.f / M_PI);
		vecAngles[1] = (float)atan(vecDelta[1] / vecDelta[0])	*(float)(180.f / M_PI);
		vecAngles[2] = (float)0.f;

		if (vecDelta[0] >= 0.f) vecAngles[1] += 180.0f;
		return vecAngles;
	}

	/* 角度归一化 */
	void angle_normalize(Vec3& vAngles)
	{
		for (int i = 0; i < 3; i++)
		{
			if (vAngles[i] < -180.0f) vAngles[i] += 360.0f;
			if (vAngles[i] > 180.0f) vAngles[i] -= 360.0f;
		}

		if (vAngles.x < -89.0f) vAngles.x = 89.0f;
		if (vAngles.x > 89.0f) vAngles.x = 89.0f;

		vAngles.z = 0;
	}

	/* 规范角度 */
	void clamp_angles(Vec3& vAngles)
	{
		while (vAngles.y < -180.0f) vAngles.y += 360.0f;
		while (vAngles.y > 180.0f) vAngles.y -= 360.0f;

		if (vAngles.x < -89.0f) vAngles.x = 89.0f;
		if (vAngles.x > 89.0f) vAngles.x = 89.0f;

		vAngles.z = 0;
	}

	/* 信息的更新 */
	void info_update()
	{
		// 获取自己基址
		DWORD64 addr = m_driver.read<DWORD64>(m_driver.m_base + apex_offsets::LocalPlayer);
		m_local.update(&m_driver, addr);

		// 读取全部客户端信息
		m_driver.read_array(m_driver.m_base + apex_offsets::cl_entitylist, g_clients, sizeof(client_info) * NUM_ENT_ENTRIES);

		// 解析玩家
		int num = get_visiable_player();
		std::cout << std::oct << "[+] 玩家[" << num << "]名" << std::endl;
	}

	/* 玩家辉光 */
	void glow_player(bool state)
	{
		// 自己基址为空
		if (m_local.empty()) return;

		// 玩家死亡
		if (m_local.get_current_health() <= 0) return;

		// 遍历玩家
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			// 空判断
			if (m_players[i].empty()) break;

			// 玩家存活判断
			if (m_players[i].get_current_health() <= 0) continue;
			if (m_players[i].is_life() == false) continue;

			// 玩家是队友
			if (m_players[i].get_team_id() == m_local.get_team_id()) continue;

			// 玩家辉光
			m_players[i].glow_player(state);
		}
	}

	/* 玩家自瞄 */
	void aim_player()
	{
		// 自己基址为空
		if (m_local.empty()) return;

		// 玩家死亡
		if (m_local.get_current_health() <= 0) return;

		// 获取头部骨骼
		Vec3 local_head = m_local.get_bone_position(2);

		// 获取当前角度
		Vec3 current_angle = m_local.get_angle();

		// 获取后坐力角度
		Vec3 recoil_angle = m_local.get_recoil_angle();

		Vec3 vest;
		float max_fov = 20.0f;
		bool state = false;

		// 遍历玩家
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			// 后面的全部为空
			if (m_players[i].empty()) break;

			// 玩家死亡
			if (m_players[i].get_current_health() <= 0) continue;

			// 不存活
			if (m_players[i].is_life() == false) continue;

			// 玩家是队友
			if (m_players[i].get_team_id() == m_local.get_team_id()) continue;

			// 获取玩家骨骼
			Vec3 v = m_players[i].get_bone_position(2);

			// 距离超过3000就不要自瞄了
			float dis = local_head.distance(v);
			if (dis > 3000.0f) continue;

			// 找到距离完美准星最近的那一个敌人
			float f = get_max_fov(current_angle, local_head, v);
			if (f < max_fov)
			{
				max_fov = f;
				vest = v;
				state = true;
			}
		}

		if (state)
		{
			// 计算自瞄角度
			Vec3 angle = get_aimbot_angle(local_head, vest) - recoil_angle;

			// 归一化角度
			angle_normalize(angle);
			clamp_angles(angle);

			// 设置角度
			m_local.set_angle(angle);
		}
	}

	/* 开始作弊 */
	void start_cheats()
	{
		// 初始化
		if (initialize() == false) return;

		// 状态更新
		info_update();

		// 循环
		while (true)
		{
			// 游戏窗口是顶层窗口
			if (m_hwnd == GetForegroundWindow())
			{
				// 跳跃键更新信息和设置玩家辉光
				// 因为APEX是64位的游戏,一直状态更新的话,自瞄速度跟不上啊
				if (GetAsyncKeyState(VK_SPACE) & 0x8000)
				{
					// 状态更新
					info_update();

					// 玩家辉光
					glow_player(true);
				}

				// 玩家自瞄
				if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) aim_player();

				// 退出作弊
				if (GetAsyncKeyState(VK_F9) & 0x8000) break;
			}

			// 放过CPU
			Sleep(5);
		}

		// 状态更新
		info_update();

		// 结束玩家辉光
		glow_player(false);
	}
};