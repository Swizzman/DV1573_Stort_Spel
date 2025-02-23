﻿#include "EnginePCH.h"
#include "Engine.h"
#include <omp.h>
#include "Camera.h"
#include "GridSystem.h"
#include "OptionSystem.h"
#include "Picture.h"
#include "Canvas.h"

/*
	We only need the loading screen rendered once. 
*/
static bool s_loaded = false;

bool Engine::s_safeExit = false;

Engine::Engine()
	: BasicEngine()
{
	LOG_INFO("Engine(): " __TIMESTAMP__);
	SoundHandler::Get();
	OptionSystem::Get().OnStartUp();
}

void Engine::Startup()
{
	T_INIT(1, thread::ThreadType::POOL_FIFO);
	srand(static_cast<unsigned>(time(NULL)));

	// Window Startup:
	Window::Desc config;

	//Get heighest possible 16:9 resolution
	//90% of the height
	//config.height = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN) * 0.50f);
	//float aspectRatio = 16.0f / 9.0f;
	//config.width = static_cast<UINT>(aspectRatio * config.height);


	int fullscreen = std::stoi(OptionSystem::Get().GetOption("Fullscreen"));
	if (fullscreen == 0)
	{
		config.height = std::stoi(OptionSystem::Get().GetOption("WindowHeight"));
		config.width = std::stoi(OptionSystem::Get().GetOption("WindowWidth"));
		if ((config.width | config.height) == 0)
		{
			config.height = 720;
			config.width = 1280;

			OptionSystem::Get().SetOption("WindowHeight", "720");
			OptionSystem::Get().SetOption("WindowWidth", "1280");
		}
	}

	config.title = L"Homehearth";
	if (!m_window.Initialize(config))
	{
		LOG_ERROR("Could not Initialize m_window.");
	}

	// DirectX Startup:
	D3D11Core::Get().Initialize(&m_window);
	D2D1Core::Initialize(&m_window);

	IMGUI(
		// Setup ImGUI
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO & io = ImGui::GetIO();
		ImGui_ImplWin32_Init(m_window.GetHWnd());
		ImGui_ImplDX11_Init(D3D11Core::Get().Device(), D3D11Core::Get().DeviceContext());
		ImGui::StyleColorsDark();
		ImGui_ImplDX11_CreateDeviceObjects(); // uses device, therefore has to be called before render thread starts
		LOG_INFO("ImGui was successfully initialized");
	);

	this->SetupLoadingScreen();
	SetScene("Loading");
	if (thread::IsThreadActive())
		T_CJOB(Engine, RenderThread);

	m_renderer.Initialize(&m_window);
	m_renderer.Setup(*this);

	// Thread Startup.
	thread::RenderThreadHandler::Get().SetRenderer(&m_renderer);
	thread::RenderThreadHandler::Get().SetWindow(&m_window);
	thread::RenderThreadHandler::Get().Setup(2);

	InputSystem::Get().SetMouseWindow(m_window.GetHWnd(), m_window.GetWidth(), m_window.GetHeight());

	SoundHandler::Get().SetMasterVolume(std::stof(OptionSystem::Get().GetOption("MasterVolume")));
	SoundHandler::Get().Setup();
	
	BasicEngine::Startup();
}

void Engine::Run()
{
	BasicEngine::Run();
	// Wait for the rendering thread to exit its last render cycle and shutdown
#if _DEBUG
	// This is debug since it catches release in endless loop.
	while (!s_safeExit) {};
#endif
	
	IMGUI(
		// ImGUI Shutdown
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	);

    T_DESTROY();
    D2D1Core::Destroy();
	ResourceManager::Get().Destroy();
}


Window* Engine::GetWindow()
{
	return &m_window;
}

void Engine::drawImGUI()
{
	//Containers for plotting
	static std::vector<float> fpsContainer;
	static std::vector<float> fpsUpdateContainer;
	static std::vector<float> fpsNetworkContainer;
	static std::vector<float> ramUsageContainer;
	static std::vector<float> vRamUsageContainer;

	static Timer timer;
	static int dots = 0;
	if (timer.GetElapsedTime<std::chrono::duration<float>>() > 0.5f)
	{
		fpsContainer.emplace_back(1.f / Stats::Get().GetFrameTime());
		fpsUpdateContainer.emplace_back(1.f / Stats::Get().GetUpdateTime());
		fpsNetworkContainer.emplace_back(1.f / Stats::Get().GetNetworkTime());
		ramUsageContainer.emplace_back((Profiler::GetRAMUsage() / (1024.f * 1024.f)));
		vRamUsageContainer.emplace_back((Profiler::GetVRAMUsage() / (1042.f * 1024.f)));
		timer.Start();
		dots = (dots + 1) % 4;
	}

	if (fpsContainer.size() > 10)
		fpsContainer.erase(fpsContainer.begin());

	if (fpsUpdateContainer.size() > 10)
		fpsUpdateContainer.erase(fpsUpdateContainer.begin());

	if (fpsNetworkContainer.size() > 10)
		fpsNetworkContainer.erase(fpsNetworkContainer.begin());

	if (ramUsageContainer.size() > 10)
		ramUsageContainer.erase(ramUsageContainer.begin());

	if (vRamUsageContainer.size() > 10)
		vRamUsageContainer.erase(vRamUsageContainer.begin());

	ImGui::Begin("Window");
	const std::string screenRes = "Screen Resolution: " + std::to_string(m_window.GetWidth()) + "x" + std::to_string(m_window.GetHeight());
	ImGui::Text("%s", screenRes.c_str());
	ImGui::End();


	ImGui::Begin("Statistics");
#if PROFILER
	static bool isRecProfileSession = false;
	if (!isRecProfileSession)
	{
		if (ImGui::Button("Record"))
		{
			isRecProfileSession = true;
			PROFILER_BEGIN_SESSION();
		}
		ImGui::SameLine();
		ImGui::Text("- Starts Profiler Session");
	}
	else
	{
		if (ImGui::Button("Stop Recording"))
		{
			isRecProfileSession = false;
			PROFILER_END_SESSION();
		}
		ImGui::SameLine();
		std::string loadingDots = "";
		for (int i = 0; i < dots; i++)
			loadingDots.append(".");
		ImGui::TextColored(ImColor(1.f, 0.f, 0.f, 1.0f), ("Recording" + loadingDots).c_str());
	}
#endif
	if (ImGui::CollapsingHeader("FPS"))
	{
		ImGui::PlotLines(("FPS: " + std::to_string(1.f / Stats::Get().GetFrameTime())).c_str(), fpsContainer.data(), static_cast<int>(fpsContainer.size()), 0, nullptr, 0.0f, Stats::Get().GetFramerate(), ImVec2(150, 50));
		ImGui::Spacing();
		ImGui::PlotLines(("Update FPS: " + std::to_string(1.f / Stats::Get().GetUpdateTime())).c_str(), fpsUpdateContainer.data(), static_cast<int>(fpsUpdateContainer.size()), 0, nullptr, 0.0f, Stats::Get().GetUpdaterate(), ImVec2(150, 50));
		ImGui::Spacing();
		ImGui::PlotLines(("Network FPS: " + std::to_string(1.f / Stats::Get().GetNetworkTime())).c_str(), fpsNetworkContainer.data(), static_cast<int>(fpsNetworkContainer.size()), 0, nullptr, 0.0f, Stats::Get().GetTickrate(), ImVec2(150, 50));
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Memory"))
	{
		ImGui::PlotHistogram(("RAM: " + std::to_string(Profiler::GetRAMUsage() / (1024.f * 1024.f)) + " MB").c_str(), ramUsageContainer.data(), static_cast<int>(ramUsageContainer.size()), 0, nullptr, 0.0f, 500.0f, ImVec2(150, 75));
		ImGui::Spacing();
		ImGui::PlotHistogram(("VRAM: " + std::to_string(Profiler::GetVRAMUsage() / (1024.f * 1024.f)) + " MB").c_str(), vRamUsageContainer.data(), static_cast<int>(vRamUsageContainer.size()), 0, nullptr, 0.0f, 500.0f, ImVec2(150, 75));
	}

	ImGui::End();

	ImGui::Begin("Components");
	if (ImGui::CollapsingHeader("Transform"))
	{
		
		GetCurrentScene()->ForEachComponent<comp::Transform>([&](Entity& e, comp::Transform& transform)
			{
				std::string entityname = "Entity: " + std::to_string(static_cast<int>((entt::entity)e));
				
				ImGui::Separator();
				ImGui::Text(entityname.c_str());
				ImGui::DragFloat3(("Position##" + std::to_string(static_cast<int>((entt::entity)e))).c_str(), (float*)&transform.position);
				ImGui::DragFloat3(("Rotation##" + std::to_string(static_cast<int>((entt::entity)e))).c_str(), (float*)&transform.rotation, dx::XMConvertToRadians(1.f));

				if (ImGui::Button(("Remove##" + std::to_string(static_cast<int>((entt::entity)e))).c_str()))
				{
					e.Destroy();
				}
				ImGui::Spacing();
			});
	}
	
	if (ImGui::CollapsingHeader("Renderable"))
	{
		GetCurrentScene()->ForEachComponent<comp::Renderable>([&](Entity& e, comp::Renderable& renderable)
			{
				std::string entityname = "Entity: " + std::to_string(static_cast<int>((entt::entity)e));

				ImGui::Separator();
				ImGui::Text(entityname.c_str());
				ImGui::Text("Change 'mtl-file'");
				char str[30] = "";
				ImGui::InputText(entityname.c_str(), str, IM_ARRAYSIZE(str));
				if (ImGui::IsKeyPressedMap(ImGuiKey_Enter))
				{
					renderable.model->ChangeMaterial(str);
				}
				ImGui::Spacing();
			});
	}

	if (ImGui::CollapsingHeader("Animators"))
	{
		GetCurrentScene()->ForEachComponent<comp::Animator>([&](Entity& e, comp::Animator& animComp)
			{
				std::string entityname = "Entity: " + std::to_string(static_cast<int>((entt::entity)e));

				const char* items[] = { "NONE", "IDLE", "WALK", "RUN", "PRIMARY_ATTACK", 
										"SECONDARY_ATTACK", "ABILITY1", "ABILITY2", 
										"ABILITY3", "ABILITY4", "TAKE_DAMAGE", "PLACE_DEFENCE" };
				int index = (int)animComp.animator->GetCurrentState();

				if (ImGui::ListBox(entityname.c_str(), &index, items, IM_ARRAYSIZE(items), 3))
				{
					if (animComp.animator)
					{
						animComp.animator->ChangeAnimation(EAnimationType(index));
					}
				}
				ImGui::Spacing();
			});
	}

	if (ImGui::CollapsingHeader("Light"))
	{
		GetCurrentScene()->ForEachComponent<comp::Light>([&](Entity& e, comp::Light& light)
			{
				std::string entityname = "Entity: " + std::to_string(static_cast<int>((entt::entity)e));
				
				ImGui::Separator();
				ImGui::Text(entityname.c_str());
				ImGui::SameLine();
				std::string index = std::to_string(light.index);
				ImGui::Text("Light index: %d", light.index);

				bool edited = false;
				if (ImGui::ColorEdit4(("Color##" + index).c_str(), (float*)&light.lightData.color)) 
					edited = true;
				
				if (ImGui::DragFloat3(("Direction##" + index).c_str(), (float*)&light.lightData.direction))
					edited = true;

				if (ImGui::Checkbox(("Enabled##" + index).c_str(), (bool*)&light.lightData.enabled))
					edited = true;

				if (ImGui::DragFloat3(("Position##" + index).c_str(), (float*)&light.lightData.position))
					edited = true;
				
				if (ImGui::InputFloat(("Range##" + index).c_str(), &light.lightData.range))
					edited = true;
				
				const char* const items[2] = { "Directional", "Point" };
				if (ImGui::ListBox(("Type##" + index).c_str(), (int*)&light.lightData.type, items, 2))
					edited = true;
				
				if (edited)
				{
					GetCurrentScene()->GetLights()->EditLight(light.lightData, light.index);
				}

				ImGui::Spacing();

			});

	}
	if (ImGui::CollapsingHeader("Network"))
	{

		GetCurrentScene()->ForEachComponent<comp::Network>([&](Entity& e, comp::Network& net)
			{
				std::string entityname = "Entity: " + std::to_string(static_cast<int>((entt::entity)e));
				std::string netID = "Network ID: " + std::to_string(net.id);

				ImGui::Separator();
				ImGui::Text(entityname.c_str());
				ImGui::Text(netID.c_str());

				ImGui::Spacing();
			});

	}
	if (ImGui::CollapsingHeader("Colliders"))
	{
		ImGui::Text("Bounding Oriented Box");
		ImGui::Spacing();

		GetCurrentScene()->ForEachComponent<comp::OrientedBoxCollider>([&](Entity& e, comp::OrientedBoxCollider& box)
			{
				std::string id = std::to_string(static_cast<int>((entt::entity)e));
				
				ImGui::Text("Entity: %d", (entt::entity)e);
				
				ImGui::DragFloat3(("Center##" + id).c_str(), (float*)&box.Center);
				ImGui::DragFloat3(("Extents##" + id).c_str(), (float*)&box.Extents);
				ImGui::DragFloat3(("Orientation##" + id).c_str(), (float*)&box.Orientation);

				ImGui::Spacing();
				ImGui::Separator();
			});

		ImGui::Text("Bounding Sphere");
		ImGui::Spacing();
		GetCurrentScene()->ForEachComponent<comp::SphereCollider>([&](Entity& e, comp::SphereCollider& s)
			{
				std::string id = std::to_string(static_cast<int>((entt::entity)e));

				ImGui::Text("Entity: %d", (entt::entity)e);

				ImGui::DragFloat3(("Center##" + id).c_str(), (float*)&s.Center);
				ImGui::DragFloat(("Radius##" + id).c_str(), (float*)&s.Radius, 0.1f);

				ImGui::Spacing();
				ImGui::Separator();
			});

	}

	ImGui::End();

	ImGui::Begin("Camera");
	{
		Camera* currentCam = GetCurrentScene()->GetCurrentCamera();
		if (currentCam)
		{
			const std::string position = "Position: " + std::to_string(currentCam->GetPosition().x)+ " " + std::to_string(currentCam->GetPosition().y) + " " + std::to_string(currentCam->GetPosition().z);
			ImGui::Separator();
			ImGui::Text(position.c_str());
			ImGui::DragFloat("Zoom: ", &currentCam->m_zoomValue, 0.01f, 0.0001f, 1.0f);
			ImGui::DragFloat("Near Plane : ", &currentCam->m_nearPlane, 0.1f , 0.0001f, currentCam->m_farPlane-1);
			ImGui::DragFloat("Far Plane: ", &currentCam->m_farPlane, 0.1f, currentCam->m_nearPlane+1);
			ImGui::DragFloat3("Position: ", (float*)&currentCam->m_position, 0.1f);
			ImGui::DragFloat3("Rotation: ", (float*)&currentCam->m_rollPitchYaw, 0.1f, 0.0f);
			ImGui::Spacing();
		}
		else
		{
			ImGui::Text("No Camera");
		}
	};
	ImGui::End();


	ImGui::Begin("Render Pass");
	{
		ImGui::Checkbox("Render Colliders", GetCurrentScene()->GetIsRenderingColliders());
	};

	int size = m_renderer.GetShadowMapSize();
	if (ImGui::InputInt("ShadowMapResolution", &size, 1024))
	{
		size = max(size, 64);
		m_renderer.SetShadowMapSize(size);
	}

	if (ImGui::CollapsingHeader("Preview ShadowMap"))
	{
		m_renderer.ImGuiShowTextures();
	}

	ImGui::End();
	
}

void Engine::SetupLoadingScreen()
{
	const float width = (float)GetWindow()->GetWidth();
	const float height = (float)GetWindow()->GetHeight();
	Scene& scene = GetScene("Loading");

	Collection2D* loadingScreen = new Collection2D;

	loadingScreen->AddElement<rtd::Picture>("LoadingScreen.png", (draw_t(0.0f, 0.0f, width, height)));
	loadingScreen->AddElement<rtd::Canvas>(D2D1::ColorF(0, 0.0f), draw_t(0.0f, 0.0f, width / 2.0f, height / 2.0f));

	scene.Add2DCollection(loadingScreen, "LoadingScreen");
}

void Engine::RenderThread()
{
	double currentFrame = 0.f;
	double lastFrame = omp_get_wtime();
	float deltaTime = 0.f;
	float frameTime = 0.f;

	//Need to place inside of the loop if we have to update it from settings
	const float TARGET_DELTA = 1.f / Stats::Get().GetFramerate();

	while (IsRunning())
	{
		currentFrame = omp_get_wtime();
		deltaTime = static_cast<float>(currentFrame - lastFrame);
		frameTime += deltaTime;

		//Render every now and then
		if (frameTime >= TARGET_DELTA)
		{
			if (!s_loaded && GetCurrentScene() == &GetScene("Loading"))
			{
				GetCurrentScene()->Update2D();
				D2D1Core::Begin();
				GetCurrentScene()->Render2D();
				D2D1Core::Present();
				D3D11Core::Get().SwapChain()->Present(0, 0);
				s_loaded = true;
				frameTime = 0.f;
			}
			if (GetCurrentScene()->IsRenderReady()) // Render Scene.
			{
				Stats::Get().SetFrameTime(frameTime);
				Render();
				frameTime = 0.f;
				s_loaded = false;
			}
		}
		
		lastFrame = currentFrame;
	}

	s_safeExit = true;
}

void Engine::Update(float dt)
{
	PROFILE_FUNCTION();

	InputSystem::Get().UpdateEvents();

	MSG msg = { nullptr };
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (msg.message == WM_QUIT)
		{
			Shutdown();
		}
	}
	// todo temp
	/*if (InputSystem::Get().CheckMouseKey(MouseKey::RIGHT, KeyState::PRESSED))
	{
		InputSystem::Get().SwitchMouseMode();

		LOG_INFO("Switched mouse Mode");
	}*/

	{
		PROFILE_SCOPE("Starting ImGui");
		IMGUI(
			m_imguiMutex.lock();
			// Start ImGui frame
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
		);
	}

	OnUserUpdate(Stats::Get().GetUpdateTime());
	BasicEngine::Update(Stats::Get().GetUpdateTime());

	{
		PROFILE_SCOPE("Ending ImGui");

		IMGUI(
			drawImGUI();

			ImGui::EndFrame();
			m_imguiMutex.unlock();
		);
	}
}

void Engine::Render()
{
	PROFILE_FUNCTION();

	{
		PROFILE_SCOPE("Render D3D11");
		/*
			Render 3D
		*/
		m_renderer.ClearFrame();

		m_renderer.Render(GetCurrentScene());
	}

#if RENDER_INGAME_UI
	{
		PROFILE_SCOPE("Render D2D1");
		D2D1Core::Begin();
		GetCurrentScene()->Render2D();
		D2D1Core::Present();
}
#else
	{
		if (GetCurrentScene() != &GetScene("Game"))
		{
			PROFILE_SCOPE("Render D2D1");
			D2D1Core::Begin();
			GetCurrentScene()->Render2D();
			D2D1Core::Present();
		}		
	}
#endif	

	{
		PROFILE_SCOPE("Render ImGui");
		IMGUI(
			m_imguiMutex.lock();
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		m_imguiMutex.unlock();
		);
	}
	
	{
		PROFILE_SCOPE("Present");
		D3D11Core::Get().SwapChain()->Present(0, 0);
	}


}
