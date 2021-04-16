 #include "pch.h"
#include "holo_winrtMain.h"
#include "Common/DirectXHelper.h"

#include <sstream>

#include <windows.graphics.directx.direct3d11.interop.h>

using namespace holo_winrt;
using namespace concurrency;
using namespace Microsoft::WRL;
using namespace std::placeholders;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Graphics::Holographic;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Foundation::Metadata;

// Loads and initializes application assets when the application is loaded.
holo_winrtMain::holo_winrtMain(std::shared_ptr<DX::DeviceResources> const& deviceResources) :
    m_deviceResources(deviceResources)
{
    // Register to be notified if the device is lost or recreated.
    m_deviceResources->RegisterDeviceNotify(this);

    // If connected, a game controller can also be used for input.
    m_gamepadAddedEventToken = Gamepad::GamepadAdded(bind(&holo_winrtMain::OnGamepadAdded, this, _1, _2));
    m_gamepadRemovedEventToken = Gamepad::GamepadRemoved(bind(&holo_winrtMain::OnGamepadRemoved, this, _1, _2));

    for (Gamepad const& gamepad : Gamepad::Gamepads())
    {
        OnGamepadAdded(nullptr, gamepad);
    }

    m_canGetHolographicDisplayForCamera = ApiInformation::IsPropertyPresent(winrt::name_of<HolographicCamera>(), L"Display");
    m_canGetDefaultHolographicDisplay = ApiInformation::IsMethodPresent(winrt::name_of<HolographicDisplay>(), L"GetDefault");
    m_canCommitDirect3D11DepthBuffer = ApiInformation::IsMethodPresent(winrt::name_of<HolographicCameraRenderingParameters>(), L"CommitDirect3D11DepthBuffer");
    m_canUseWaitForNextFrameReadyAPI = ApiInformation::IsMethodPresent(winrt::name_of<HolographicSpace>(), L"WaitForNextFrameReady");

    if (m_canGetDefaultHolographicDisplay)
    {
        // Subscribe for notifications about changes to the state of the default HolographicDisplay 
        // and its SpatialLocator.
        m_holographicDisplayIsAvailableChangedEventToken = HolographicSpace::IsAvailableChanged(bind(&holo_winrtMain::OnHolographicDisplayIsAvailableChanged, this, _1, _2));
    }

    // Acquire the current state of the default HolographicDisplay and its SpatialLocator.
    OnHolographicDisplayIsAvailableChanged(nullptr, nullptr);
}

void holo_winrtMain::SetHolographicSpace(HolographicSpace const& holographicSpace)
{
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    //
    // TODO: Add code here to initialize your holographic content.
    //

#ifdef DRAW_SAMPLE_CONTENT
    // Initialize the sample hologram.
    m_spinningCubeRenderer = std::make_unique<SpinningCubeRenderer>(m_deviceResources);
    m_spinningCubeRenderer2 = std::make_unique<SpinningCubeRenderer>(m_deviceResources);
    m_spatialInputHandler = std::make_unique<SpatialInputHandler>();

    m_handMeshRenderer = nullptr;
#endif

    // Respond to camera added events by creating any resources that are specific
    // to that camera, such as the back buffer render target view.
    // When we add an event handler for CameraAdded, the API layer will avoid putting
    // the new camera in new HolographicFrames until we complete the deferral we created
    // for that handler, or return from the handler without creating a deferral. This
    // allows the app to take more than one frame to finish creating resources and
    // loading assets for the new holographic camera.
    // This function should be registered before the app creates any HolographicFrames.
    m_cameraAddedToken = m_holographicSpace.CameraAdded(std::bind(&holo_winrtMain::OnCameraAdded, this, _1, _2));

    // Respond to camera removed events by releasing resources that were created for that
    // camera.
    // When the app receives a CameraRemoved event, it releases all references to the back
    // buffer right away. This includes render target views, Direct2D target bitmaps, and so on.
    // The app must also ensure that the back buffer is not attached as a render target, as
    // shown in DeviceResources::ReleaseResourcesForBackBuffer.
    m_cameraRemovedToken = m_holographicSpace.CameraRemoved(std::bind(&holo_winrtMain::OnCameraRemoved, this, _1, _2));

    // Notes on spatial tracking APIs:
    // * Stationary reference frames are designed to provide a best-fit position relative to the
    //   overall space. Individual positions within that reference frame are allowed to drift slightly
    //   as the device learns more about the environment.
    // * When precise placement of individual holograms is required, a SpatialAnchor should be used to
    //   anchor the individual hologram to a position in the real world - for example, a point the user
    //   indicates to be of special interest. Anchor positions do not drift, but can be corrected; the
    //   anchor will use the corrected position starting in the next frame after the correction has
    //   occurred.
}

void holo_winrtMain::UnregisterHolographicEventHandlers()
{
    if (m_holographicSpace != nullptr)
    {
        // Clear previous event registrations.
        m_holographicSpace.CameraAdded(m_cameraAddedToken);
        m_cameraAddedToken = {};
        m_holographicSpace.CameraRemoved(m_cameraRemovedToken);
        m_cameraRemovedToken = {};
    }

    if (m_spatialLocator != nullptr)
    {
        m_spatialLocator.LocatabilityChanged(m_locatabilityChangedToken);
    }
}

holo_winrtMain::~holo_winrtMain()
{
    // Deregister device notification.
    m_deviceResources->RegisterDeviceNotify(nullptr);

    UnregisterHolographicEventHandlers();

    Gamepad::GamepadAdded(m_gamepadAddedEventToken);
    Gamepad::GamepadRemoved(m_gamepadRemovedEventToken);
    HolographicSpace::IsAvailableChanged(m_holographicDisplayIsAvailableChangedEventToken);
}

// Updates the application state once per frame.
HolographicFrame holo_winrtMain::Update(HolographicFrame const& previousFrame)
{
    // TODO: Put CPU work that does not depend on the HolographicCameraPose here.

    // Apps should wait for the optimal time to begin pose-dependent work.
    // The platform will automatically adjust the wakeup time to get
    // the lowest possible latency at high frame rates. For manual
    // control over latency, use the WaitForNextFrameReadyWithHeadStart 
    // API.
    // WaitForNextFrameReady and WaitForNextFrameReadyWithHeadStart are the
    // preferred frame synchronization APIs for Windows Mixed Reality. When 
    // running on older versions of the OS that do not include support for
    // these APIs, your app can use the WaitForFrameToFinish API for similar 
    // (but not as optimal) behavior.
    if (m_canUseWaitForNextFrameReadyAPI)
    {
        try
        {
            m_holographicSpace.WaitForNextFrameReady();
        }
        catch (winrt::hresult_not_implemented const& /*ex*/)
        {
            // Catch a specific case where WaitForNextFrameReady() is present but not implemented
            // and default back to WaitForFrameToFinish() in that case.
            m_canUseWaitForNextFrameReadyAPI = false;
        }
    }
    else if (previousFrame)
    {
        previousFrame.WaitForFrameToFinish();
    }

    // Before doing the timer update, there is some work to do per-frame
    // to maintain holographic rendering. First, we will get information
    // about the current frame.

    // The HolographicFrame has information that the app needs in order
    // to update and render the current frame. The app begins each new
    // frame by calling CreateNextFrame.
    HolographicFrame holographicFrame = m_holographicSpace.CreateNextFrame();

    // Get a prediction of where holographic cameras will be when this frame
    // is presented.
    HolographicFramePrediction prediction = holographicFrame.CurrentPrediction();

    // Back buffers can change from frame to frame. Validate each buffer, and recreate
    // resource views and depth buffers as needed.
    m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

#ifdef DRAW_SAMPLE_CONTENT
    if (m_stationaryReferenceFrame != nullptr)
    {
        // Check for new input state since the last frame.
        for (GamepadWithButtonState& gamepadWithButtonState : m_gamepads)
        {
            bool buttonDownThisUpdate = ((gamepadWithButtonState.gamepad.GetCurrentReading().Buttons & GamepadButtons::A) == GamepadButtons::A);
            if (buttonDownThisUpdate && !gamepadWithButtonState.buttonAWasPressedLastFrame)
            {
                m_pointerPressed = true;
            }
            gamepadWithButtonState.buttonAWasPressedLastFrame = buttonDownThisUpdate;
        }
        
        // Check if pointer has been pressed
        SpatialInteractionSourceState pointerState = m_spatialInputHandler->CheckForInput();
        SpatialPointerPose pose = nullptr;

        if (pointerState != nullptr)
        {
            pose = pointerState.TryGetPointerPose(m_stationaryReferenceFrame.CoordinateSystem());
        }
        else if (m_pointerPressed)
        {
            pose = SpatialPointerPose::TryGetAtTimestamp(m_stationaryReferenceFrame.CoordinateSystem(), prediction.Timestamp());
        }
        m_pointerPressed = false;

        IVectorView<SpatialInteractionSourceState> detectedSources = m_spatialInputHandler->CheckForDetectedSources(prediction);
        unsigned int arrSize = detectedSources.Size();

        if (detectedSources.Size() > 0)
        {
            for (auto s : detectedSources)
            {
                // Lets see if we've are dealing with the right hand
                if (s.Source().Handedness() == SpatialInteractionSourceHandedness(2))
                {
                    People::HandPose myHand = s.TryGetHandPose();

                    if (myHand != nullptr)
                    {
                        People::HandJointKind littleFinger = People::HandJointKind(25);
                        People::JointPose littleFingerPose;

                        int handID = s.Source().Id();

                        // If stored Id for right hand doesnt match newly found id then we must
                        // create a new thread
                        if (m_RightHandId != handID)
                        {
                            if (m_handThreadRunning == true) { continue; };
                            std::thread createObserverThread([this, s, myHand, handID]()
                                {
                                    m_handThreadRunning = true;
                                    // hold this object as long as source is active
                                    People::HandMeshObserver const& newHandMeshObserver = s.Source().TryCreateHandMeshObserverAsync().get();
                                    if (newHandMeshObserver)
                                    {
                                        std::lock_guard<std::mutex> guard(m_lockHandFetch);
                                        unsigned short indexCount = newHandMeshObserver.TriangleIndexCount();
                                        std::vector<unsigned short> indices(indexCount);

                                        newHandMeshObserver.GetTriangleIndices(indices);

                                        // save indices for later use - as they dont change

                                        // store ref to current hand observer
                                        m_currentHandMeshObserver = newHandMeshObserver;

                                        // store indices and i's count already as they wont change 
                                        // m_currentHandMeshIndicesCount = indexCount;
                                        // m_currentHandMeshIndices = indices;

                                        // get vertices from init as well
                                        std::vector<People::HandMeshVertex> vertices(m_currentHandMeshObserver.VertexCount());
                                        auto vertexState = m_currentHandMeshObserver.GetVertexStateForPose(myHand);
                                        vertexState.GetVertices(vertices);

                                        // create our new hand renderer
                                        if (m_handMeshRenderer != nullptr)
                                        {
                                            m_handMeshRenderer.reset();
                                        }
                                        
                                        m_handMeshRenderer = std::make_unique<HandMeshRenderer>(m_deviceResources, vertices, indices);

                                        // Set Index data
                                        // m_handMeshRenderer->SetHandIndices(indices);

                                        m_RightHandId = handID;

                                        std::wstringstream s;
                                        s << "RightHandId: " << m_RightHandId << '\ n';
                                        std::wstring ws = s.str();

                                        OutputDebugString(ws.c_str());

                                        m_handThreadRunning = false;
                                    }
                                });
                            createObserverThread.detach();
                        }
                        else
                        {
                            if (m_currentHandMeshObserver != nullptr)
                            {

                                People::HandPose curr_myHand = s.TryGetHandPose();

                                if (curr_myHand != nullptr)
                                {
                                    std::lock_guard<std::mutex> guard(m_lockHandFetch);
                                    // vertex count of fetched hand mesh
                                    uint32_t curr_v_count = m_currentHandMeshObserver.VertexCount();

                                    // transfer vertices from hand mesh observer object to vector var
                                    std::vector<People::HandMeshVertex> curr_vertices(curr_v_count);
                                    auto vertexState = m_currentHandMeshObserver.GetVertexStateForPose(curr_myHand);
                                    vertexState.GetVertices(curr_vertices);

                                    auto meshTransform = vertexState.CoordinateSystem().TryGetTransformTo(m_stationaryReferenceFrame.CoordinateSystem());
                                    if (meshTransform != nullptr)
                                    {
                                        // Now we have verticies and indices(saved earlier) that we can use to render the right hand
                                        // Lets update our HandMeshRenderer vertex buffer with newly aquired vertices
                                        // We also need to apply appropriate coord system transform to it
                                        m_handMeshRenderer->SetModelConstantBuffer(meshTransform.Value());
                                        m_handMeshRenderer->SetVertexBufferDataSize(curr_v_count);
                                        m_handMeshRenderer->SetVertexBufferData(curr_vertices);

                                        // Lets set our transform to coord system as constant buffer

                                    }
                                }    
                            }
                        }
                        
        }

        

        if (pose != nullptr)
        {
            // Try to get current hand pose
                        
                        /*bool gotYourJoint = myHand.TryGetJoint(m_stationaryReferenceFrame.CoordinateSystem(), littleFinger, littleFingerPose);

                        if (gotYourJoint == true)
                        {
                            quaternion orientation = littleFingerPose.Orientation;
                            float3 pos = littleFingerPose.Position;

                            std::wstringstream s;
                            s << L"My Little Finger info"
                            << "Position: " << pos.x << pos.y << pos.z << '\ n'
                            << "Orientation: " << orientation.x << orientation.y << orientation.z << '\ n';

                            std::wstring ws = s.str();

                            OutputDebugString(ws.c_str());
                        }*/
                    }
}
            }
        }

        // When a Pressed gesture is detected, the sample hologram will be repositioned
        // two meters in front of the user.
        m_spinningCubeRenderer->PositionHologram(pose, 0.f, m_stationaryReferenceFrame.CoordinateSystem());
        m_spinningCubeRenderer2->PositionHologram(pose, 1.f, m_stationaryReferenceFrame.CoordinateSystem());
    }
#endif

    m_timer.Tick([this]()
    {
        //
        // TODO: Update scene objects.
        //
        // Put time-based updates here. By default this code will run once per frame,
        // but if you change the StepTimer to use a fixed time step this code will
        // run as many times as needed to get to the current step.
        //

#ifdef DRAW_SAMPLE_CONTENT
        if (m_handMeshRenderer != nullptr )
        {
                m_handMeshRenderer->Update(m_timer);
        }
        
        m_spinningCubeRenderer->Update(m_timer);
        m_spinningCubeRenderer2->Update(m_timer);
#endif
    });

    // On HoloLens 2, the platform can achieve better image stabilization results if it has
    // a stabilization plane and a depth buffer.
    // Note that the SetFocusPoint API includes an override which takes velocity as a 
    // parameter. This is recommended for stabilizing holograms in motion.
    for (HolographicCameraPose const& cameraPose : prediction.CameraPoses())
    {
#ifdef DRAW_SAMPLE_CONTENT
        // The HolographicCameraRenderingParameters class provides access to set
        // the image stabilization parameters.
        HolographicCameraRenderingParameters renderingParameters = holographicFrame.GetRenderingParameters(cameraPose);

        // SetFocusPoint informs the system about a specific point in your scene to
        // prioritize for image stabilization. The focus point is set independently
        // for each holographic camera. When setting the focus point, put it on or 
        // near content that the user is looking at.
        // In this example, we put the focus point at the center of the sample hologram.
        // You can also set the relative velocity and facing of the stabilization
        // plane using overloads of this method.
        if (m_stationaryReferenceFrame != nullptr)
        {
            renderingParameters.SetFocusPoint(
                m_stationaryReferenceFrame.CoordinateSystem(),
                m_spinningCubeRenderer->GetPosition()
            );
        }
#endif
    }

    // The holographic frame will be used to get up-to-date view and projection matrices and
    // to present the swap chain.
    return holographicFrame;
}

// Renders the current frame to each holographic camera, according to the
// current application and spatial positioning state. Returns true if the
// frame was rendered to at least one camera.
bool holo_winrtMain::Render(HolographicFrame const& holographicFrame)
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return false;
    }

    //
    // TODO: Add code for pre-pass rendering here.
    //
    // Take care of any tasks that are not specific to an individual holographic
    // camera. This includes anything that doesn't need the final view or projection
    // matrix, such as lighting maps.
    //

    // Lock the set of holographic camera resources, then draw to each camera
    // in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool>(
        [this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
    {
        // Up-to-date frame predictions enhance the effectiveness of image stablization and
        // allow more accurate positioning of holograms.
        holographicFrame.UpdateCurrentPrediction();
        HolographicFramePrediction prediction = holographicFrame.CurrentPrediction();

        bool atLeastOneCameraRendered = false;
        for (HolographicCameraPose const& cameraPose : prediction.CameraPoses())
        {
            // This represents the device-based resources for a HolographicCamera.
            DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose.HolographicCamera().Id()].get();

            // Get the device context.
            const auto context = m_deviceResources->GetD3DDeviceContext();
            const auto depthStencilView = pCameraResources->GetDepthStencilView();

            // Set render targets to the current holographic camera.
            ID3D11RenderTargetView *const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
            context->OMSetRenderTargets(1, targets, depthStencilView);

            // Clear the back buffer and depth stencil view.
            if (m_canGetHolographicDisplayForCamera &&
                cameraPose.HolographicCamera().Display().IsOpaque())
            {
                context->ClearRenderTargetView(targets[0], DirectX::Colors::CornflowerBlue);
            }
            else
            {
                context->ClearRenderTargetView(targets[0], DirectX::Colors::Transparent);
            }
            context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            //
            // TODO: Replace the sample content with your own content.
            //
            // Notes regarding holographic content:
            //    * For drawing, remember that you have the potential to fill twice as many pixels
            //      in a stereoscopic render target as compared to a non-stereoscopic render target
            //      of the same resolution. Avoid unnecessary or repeated writes to the same pixel,
            //      and only draw holograms that the user can see.
            //    * To help occlude hologram geometry, you can create a depth map using geometry
            //      data obtained via the surface mapping APIs. You can use this depth map to avoid
            //      rendering holograms that are intended to be hidden behind tables, walls,
            //      monitors, and so on.
            //    * On HolographicDisplays that are transparent, black pixels will appear transparent 
            //      to the user. On such devices, you should clear the screen to Transparent as shown 
            //      above. You should still use alpha blending to draw semitransparent holograms. 
            //


            // The view and projection matrices for each holographic camera will change
            // every frame. This function refreshes the data in the constant buffer for
            // the holographic camera indicated by cameraPose.
            if (m_stationaryReferenceFrame)
            {
                pCameraResources->UpdateViewProjectionBuffer(m_deviceResources, cameraPose, m_stationaryReferenceFrame.CoordinateSystem());
            }

            // Attach the view/projection constant buffer for this camera to the graphics pipeline.
            bool cameraActive = pCameraResources->AttachViewProjectionBuffer(m_deviceResources);

#ifdef DRAW_SAMPLE_CONTENT
            // Only render world-locked content when positional tracking is active.
            if (cameraActive)
            {
                // Draw the sample hologram.
                m_spinningCubeRenderer->Render();
                m_spinningCubeRenderer2->Render();

                if (m_handMeshRenderer != nullptr)
                {
                    m_handMeshRenderer->Render();
                }

                if (m_canCommitDirect3D11DepthBuffer)
                {
                    // On versions of the platform that support the CommitDirect3D11DepthBuffer API, we can 
                    // provide the depth buffer to the system, and it will use depth information to stabilize 
                    // the image at a per-pixel level.
                    HolographicCameraRenderingParameters renderingParameters = holographicFrame.GetRenderingParameters(cameraPose);
                    
                    IDirect3DSurface interopSurface = DX::CreateDepthTextureInteropObject(pCameraResources->GetDepthStencilTexture2D());

                    // Calling CommitDirect3D11DepthBuffer causes the system to queue Direct3D commands to 
                    // read the depth buffer. It will then use that information to stabilize the image as
                    // the HolographicFrame is presented.
                    renderingParameters.CommitDirect3D11DepthBuffer(interopSurface);
                }
            }
#endif
            atLeastOneCameraRendered = true;
        }

        return atLeastOneCameraRendered;
    });
}

void holo_winrtMain::SaveAppState()
{
    //
    // TODO: Insert code here to save your app state.
    //       This method is called when the app is about to suspend.
    //
    //       For example, store information in the SpatialAnchorStore.
    //
}

void holo_winrtMain::LoadAppState()
{
    //
    // TODO: Insert code here to load your app state.
    //       This method is called when the app resumes.
    //
    //       For example, load information from the SpatialAnchorStore.
    //
}

void holo_winrtMain::OnPointerPressed()
{
    m_pointerPressed = true;
}

// Notifies classes that use Direct3D device resources that the device resources
// need to be released before this method returns.
void holo_winrtMain::OnDeviceLost()
{
#ifdef DRAW_SAMPLE_CONTENT
    m_spinningCubeRenderer->ReleaseDeviceDependentResources();
    m_spinningCubeRenderer2->ReleaseDeviceDependentResources();
#endif
}

// Notifies classes that use Direct3D device resources that the device resources
// may now be recreated.
void holo_winrtMain::OnDeviceRestored()
{
#ifdef DRAW_SAMPLE_CONTENT
    m_spinningCubeRenderer->CreateDeviceDependentResources();
    m_spinningCubeRenderer2->CreateDeviceDependentResources();
#endif
}

void holo_winrtMain::OnLocatabilityChanged(SpatialLocator const& sender, winrt::Windows::Foundation::IInspectable const& args)
{
    switch (sender.Locatability())
    {
    case SpatialLocatability::Unavailable:
        // Holograms cannot be rendered.
    {
        winrt::hstring message(L"Warning! Positional tracking is " + std::to_wstring(int(sender.Locatability())) + L".\n");
        OutputDebugStringW(message.data());
    }
    break;

    // In the following three cases, it is still possible to place holograms using a
    // SpatialLocatorAttachedFrameOfReference.
    case SpatialLocatability::PositionalTrackingActivating:
        // The system is preparing to use positional tracking.

    case SpatialLocatability::OrientationOnly:
        // Positional tracking has not been activated.

    case SpatialLocatability::PositionalTrackingInhibited:
        // Positional tracking is temporarily inhibited. User action may be required
        // in order to restore positional tracking.
        break;

    case SpatialLocatability::PositionalTrackingActive:
        // Positional tracking is active. World-locked content can be rendered.
        break;
    }
}

void holo_winrtMain::OnCameraAdded(
    HolographicSpace const& sender,
    HolographicSpaceCameraAddedEventArgs const& args
)
{
    winrt::Windows::Foundation::Deferral deferral = args.GetDeferral();
    HolographicCamera holographicCamera = args.Camera();
    create_task([this, deferral, holographicCamera]()
    {
        //
        // TODO: Allocate resources for the new camera and load any content specific to
        //       that camera. Note that the render target size (in pixels) is a property
        //       of the HolographicCamera object, and can be used to create off-screen
        //       render targets that match the resolution of the HolographicCamera.
        //

        // Create device-based resources for the holographic camera and add it to the list of
        // cameras used for updates and rendering. Notes:
        //   * Since this function may be called at any time, the AddHolographicCamera function
        //     waits until it can get a lock on the set of holographic camera resources before
        //     adding the new camera. At 60 frames per second this wait should not take long.
        //   * A subsequent Update will take the back buffer from the RenderingParameters of this
        //     camera's CameraPose and use it to create the ID3D11RenderTargetView for this camera.
        //     Content can then be rendered for the HolographicCamera.
        m_deviceResources->AddHolographicCamera(holographicCamera);

        // Holographic frame predictions will not include any information about this camera until
        // the deferral is completed.
        deferral.Complete();
    });
}

void holo_winrtMain::OnCameraRemoved(
    HolographicSpace const& sender,
    HolographicSpaceCameraRemovedEventArgs const& args
)
{
    create_task([this]()
    {
        //
        // TODO: Asynchronously unload or deactivate content resources (not back buffer 
        //       resources) that are specific only to the camera that was removed.
        //
    });

    // Before letting this callback return, ensure that all references to the back buffer 
    // are released.
    // Since this function may be called at any time, the RemoveHolographicCamera function
    // waits until it can get a lock on the set of holographic camera resources before
    // deallocating resources for this camera. At 60 frames per second this wait should
    // not take long.
    m_deviceResources->RemoveHolographicCamera(args.Camera());
}

void holo_winrtMain::OnGamepadAdded(winrt::Windows::Foundation::IInspectable, Gamepad const& args)
{
    for (GamepadWithButtonState const& gamepadWithButtonState : m_gamepads)
    {
        if (args == gamepadWithButtonState.gamepad)
        {
            // This gamepad is already in the list.
            return;
        }
    }

    GamepadWithButtonState newGamepad = { args, false };
    m_gamepads.push_back(newGamepad);
}

void holo_winrtMain::OnGamepadRemoved(winrt::Windows::Foundation::IInspectable, Gamepad const& args)
{
    m_gamepads.erase(std::remove_if(m_gamepads.begin(), m_gamepads.end(), [&](GamepadWithButtonState& gamepadWithState)
        {
            return gamepadWithState.gamepad == args;
        }),
        m_gamepads.end());
}

void holo_winrtMain::OnHolographicDisplayIsAvailableChanged(winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable)
{
    // Get the spatial locator for the default HolographicDisplay, if one is available.
    SpatialLocator spatialLocator = nullptr;
    if (m_canGetDefaultHolographicDisplay)
    {
        HolographicDisplay defaultHolographicDisplay = HolographicDisplay::GetDefault();
        if (defaultHolographicDisplay)
        {
            spatialLocator = defaultHolographicDisplay.SpatialLocator();
        }
    }
    else
    {
        spatialLocator = SpatialLocator::GetDefault();
    }

    if (m_spatialLocator != spatialLocator)
    {
        // If the spatial locator is disconnected or replaced, we should discard all state that was
        // based on it.
        if (m_spatialLocator != nullptr)
        {
            m_spatialLocator.LocatabilityChanged(m_locatabilityChangedToken);
            m_spatialLocator = nullptr;
        }

        m_stationaryReferenceFrame = nullptr;

        if (spatialLocator != nullptr)
        {
            // Use the SpatialLocator from the default HolographicDisplay to track the motion of the device.
            m_spatialLocator = spatialLocator;

            // Respond to changes in the positional tracking state.
            m_locatabilityChangedToken = m_spatialLocator.LocatabilityChanged(std::bind(&holo_winrtMain::OnLocatabilityChanged, this, _1, _2));

            // The simplest way to render world-locked holograms is to create a stationary reference frame
            // based on a SpatialLocator. This is roughly analogous to creating a "world" coordinate system
            // with the origin placed at the device's position as the app is launched.
            m_stationaryReferenceFrame = m_spatialLocator.CreateStationaryFrameOfReferenceAtCurrentLocation();
        }
    }
}
