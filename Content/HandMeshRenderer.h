#pragma once

#include "../Common/DeviceResources.h"
#include "../Common/Steptimer.h"
#include "ShaderStructures.h"

namespace holo_winrt
{
	class HandMeshRenderer
	{
		typedef winrt::Windows::Perception::People::HandMeshVertex handMeshVertex;
	public:
		HandMeshRenderer(std::shared_ptr<DX::DeviceResources> const& deviceResources, std::vector<winrt::Windows::Perception::People::HandMeshVertex> initVertices);
		std::future<void> CreateDeviceDependentResources(std::vector<winrt::Windows::Perception::People::HandMeshVertex>);
		void ReleaseDeviceDependentResources();
		void Update(DX::StepTimer const& timer);
		void Render();

		winrt::Windows::Foundation::Numerics::float3 const& GetPosition() { return m_position; }

		void SetVertexBufferData(std::vector<handMeshVertex> arr) { m_currentHandVert = arr; };
		void SetVertexBufferDataSize(uint32_t size) { m_vertexBufferDataSize = size; };
		void SetModelConstantBuffer(winrt::Windows::Foundation::Numerics::float4x4 matrix);

		void SetHandIndices(std::vector<unsigned short>& indices);

		void TransformToStruct();


	private:
		// Cache pointer to device resources
		std::shared_ptr<DX::DeviceResources>				m_deviceResources;

		Microsoft::WRL::ComPtr<ID3D11InputLayout>			m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11Buffer>				m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>				m_indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>			m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11GeometryShader>		m_geometryShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>			m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer>				m_modelConstantBuffer;

		std::vector<unsigned short>							m_currentHandIndices;

		std::vector<handMeshVertex>							m_currentHandVert;
		uint32_t											m_currentHandVertSize;

		std::vector<VertexPositionColor>					m_vertexBufferData;
		uint32_t											m_vertexBufferDataSize;

		ModelConstantBuffer									m_modelConstantBufferData;
		uint32_t											m_indexCount = 0;

		bool												m_loadingComplete = false;
		winrt::Windows::Foundation::Numerics::float3		m_position = { 0.f, 0.f, -2.f };

		bool												m_usingVprtShaders = false;
	};
}