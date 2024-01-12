#include "OVRLipSyncConvaiPlaybackActorComponent.h"
#include "OVRLipSyncContextWrapper.h"
#include "OVRLipSyncFrame.h"

DEFINE_LOG_CATEGORY(ConvaiOVRLipSyncLog);

namespace
{
UOVRLipSyncFrameSequence *CookAudioData(uint8 *PCMData, int PCMDataSize, int SampleRate, int NumChannels)
	{

		const int16 *PCMDataInt16 = reinterpret_cast<int16*>(PCMData);
		PCMDataSize = PCMDataSize/2;

		// Compute LipSync sequence frames at 100 times a second rate
		constexpr auto LipSyncSequenceUpateFrequency = 100;
		constexpr auto LipSyncSequenceDuration = 1.0f / LipSyncSequenceUpateFrequency;

		auto Sequence = NewObject<UOVRLipSyncFrameSequence>();
		auto ChunkSizeSamples = static_cast<int>(SampleRate * LipSyncSequenceDuration);
		auto ChunkSize = NumChannels * ChunkSizeSamples;


		UOVRLipSyncContextWrapper context(ovrLipSyncContextProvider_Enhanced, SampleRate, 4096, FString());

		float LaughterScore = 0.0f;
		int32_t FrameDelayInMs = 0;
		TArray<float> Visemes;

		TArray<int16_t> samples;
		samples.SetNumZeroed(ChunkSize);
		context.ProcessFrame(samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs, NumChannels > 1);
		int FrameOffset = (int)(FrameDelayInMs * SampleRate / 1000 * NumChannels);

		for (int offs = 0; offs < PCMDataSize + FrameOffset; offs += ChunkSize)
		{
			int remainingSamples = PCMDataSize - offs;
			if (remainingSamples >= ChunkSize)
			{
				context.ProcessFrame(PCMDataInt16 + offs, ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs,
									 NumChannels > 1);
			}
			else
			{
				if (remainingSamples > 0)
				{
					memcpy(samples.GetData(), PCMDataInt16 + offs, sizeof(int16_t) * remainingSamples);
					memset(samples.GetData() + remainingSamples, 0, ChunkSize - remainingSamples);
				}
				else
				{
					memset(samples.GetData(), 0, ChunkSize);
				}
				context.ProcessFrame(samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs,
									 NumChannels > 1);
			}

			if (offs >= FrameOffset)
			{
				Sequence->Add(Visemes, LaughterScore);
			}

		}
		return Sequence;
	}
};


UConvaiOVRLipSyncComponent::UConvaiOVRLipSyncComponent() 
{ 
	PrimaryComponentTick.bCanEverTick = true; 
	CurrentSequenceTimePassed = 0;
	IsNeutralPose = true;
}


void UConvaiOVRLipSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType,
											   FActorComponentTickFunction *ThisTickFunction)
{
	if (Sequences.Num())
	{
		CurrentSequenceTimePassed += DeltaTime;
		//UE_LOG(ConvaiOVRLipSyncLog, Warning, TEXT("CurrentSequenceTimePassed: %f"), CurrentSequenceTimePassed);

		auto PlayPos = CurrentSequenceTimePassed;
		auto IntPos = static_cast<unsigned>(PlayPos * 100);
		if (IntPos >= Sequences[0]->Num())
		{
			//UE_LOG(ConvaiOVRLipSyncLog, Warning, TEXT("Sequence Done at %f"), CurrentSequenceTimePassed);
			RemoveSequence();
			InitNeutralPose();
			IsNeutralPose = true;
			CurrentSequenceTimePassed = 0;
			OnVisemesDataReady.ExecuteIfBound(); // Related to Convai LipSync Interface
			return;
		}
		const auto &Frame = (*Sequences[0])[IntPos];
		LaughterScore = Frame.LaughterScore;
		Visemes = Frame.VisemeScores;
		OnVisemesReady.Broadcast();
		OnVisemesDataReady.ExecuteIfBound(); // Related to Convai LipSync Interface
	}
	//else if (!IsNeutralPose)
	//{
	//	InitNeutralPose();
	//	IsNeutralPose = true;
	//	return;
	//}
}

void UConvaiOVRLipSyncComponent::ConvaiProcessLipSync(uint8 *InPCMData, uint32 InPCMDataSize, uint32 InSampleRate,
												uint32 InNumChannels)
{
	float SequenceDuration = float(InPCMDataSize) / float(InSampleRate * 2);
	UOVRLipSyncFrameSequence* Sequence = CookAudioData(InPCMData, InPCMDataSize, InSampleRate, InNumChannels);
	AddSequence(Sequence, SequenceDuration);
	UE_LOG(ConvaiOVRLipSyncLog, Log, TEXT("LipSync Sequence added with duration: %f, sample rate: %i and NumChannels: %i"),
		   SequenceDuration, InSampleRate, InNumChannels);
}

void UConvaiOVRLipSyncComponent::ConvaiStopLipSync()
{ 
	Sequences.Empty();
	InitNeutralPose();
	IsNeutralPose = true;
	CurrentSequenceTimePassed = 0;
}


TArray<float> UConvaiOVRLipSyncComponent::ConvaiGetVisemes()
{ 
	return GetVisemes();
}

TArray<FString> UConvaiOVRLipSyncComponent::ConvaiGetVisemeNames()
{
	return GetVisemeNames();
}
