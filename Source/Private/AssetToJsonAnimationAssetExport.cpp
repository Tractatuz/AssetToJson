#include "AssetToJsonAnimationAssetExport.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendSpace.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonTypes.h"

namespace AssetToJsonAnimationAssetPrivate
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	TSharedPtr<FJsonObject> MakeAssetRef(const UObject* Object)
	{
		TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
		if (!Object)
		{
			return JsonAsset;
		}

		JsonAsset->SetStringField(TEXT("path"), Object->GetPathName());
		JsonAsset->SetStringField(TEXT("class"), ClassPath(Object));
		return JsonAsset;
	}

	TSharedPtr<FJsonObject> ExportFrameRate(const FFrameRate& FrameRate)
	{
		TSharedPtr<FJsonObject> JsonFrameRate = MakeShared<FJsonObject>();
		JsonFrameRate->SetNumberField(TEXT("numerator"), FrameRate.Numerator);
		JsonFrameRate->SetNumberField(TEXT("denominator"), FrameRate.Denominator);
		JsonFrameRate->SetNumberField(TEXT("decimal"), FrameRate.AsDecimal());
		return JsonFrameRate;
	}

	TArray<TSharedPtr<FJsonValue>> ExportNotifies(const UAnimSequenceBase* SequenceBase)
	{
		TArray<TSharedPtr<FJsonValue>> JsonNotifies;
		if (!SequenceBase)
		{
			return JsonNotifies;
		}

		for (int32 NotifyIndex = 0; NotifyIndex < SequenceBase->Notifies.Num(); ++NotifyIndex)
		{
			const FAnimNotifyEvent& Notify = SequenceBase->Notifies[NotifyIndex];
			TSharedPtr<FJsonObject> JsonNotify = MakeShared<FJsonObject>();
			JsonNotify->SetNumberField(TEXT("index"), NotifyIndex);
			JsonNotify->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
			JsonNotify->SetStringField(TEXT("event_name"), Notify.GetNotifyEventName().ToString());
			JsonNotify->SetNumberField(TEXT("time"), Notify.GetTime());
			JsonNotify->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
			JsonNotify->SetNumberField(TEXT("end_trigger_time"), Notify.GetEndTriggerTime());
			JsonNotify->SetNumberField(TEXT("duration"), Notify.GetDuration());
			JsonNotify->SetNumberField(TEXT("track_index"), Notify.TrackIndex);
			JsonNotify->SetNumberField(TEXT("trigger_weight_threshold"), Notify.TriggerWeightThreshold);
			JsonNotify->SetStringField(TEXT("montage_tick_type"), StaticEnum<EMontageNotifyTickType::Type>()->GetNameStringByValue(Notify.MontageTickType.GetValue()));
			JsonNotify->SetBoolField(TEXT("is_branching_point"), Notify.IsBranchingPoint());
			JsonNotify->SetBoolField(TEXT("is_blueprint_notify"), Notify.IsBlueprintNotify());
			JsonNotify->SetStringField(TEXT("notify"), ObjectPath(Notify.Notify));
			JsonNotify->SetStringField(TEXT("notify_class"), ClassPath(Notify.Notify));
			JsonNotify->SetStringField(TEXT("notify_state"), ObjectPath(Notify.NotifyStateClass.Get()));
			JsonNotify->SetStringField(TEXT("notify_state_class"), ClassPath(Notify.NotifyStateClass.Get()));
			JsonNotifies.Add(MakeShared<FJsonValueObject>(JsonNotify));
		}

		return JsonNotifies;
	}

	TArray<TSharedPtr<FJsonValue>> ExportCurves(const UAnimSequenceBase* SequenceBase)
	{
		TArray<TSharedPtr<FJsonValue>> JsonCurves;
		if (!SequenceBase)
		{
			return JsonCurves;
		}

		const FRawCurveTracks& CurveData = SequenceBase->GetCurveData();
		for (const FFloatCurve& Curve : CurveData.FloatCurves)
		{
			TSharedPtr<FJsonObject> JsonCurve = MakeShared<FJsonObject>();
			JsonCurve->SetStringField(TEXT("name"), Curve.GetName().ToString());
			JsonCurve->SetStringField(TEXT("type"), TEXT("float"));
			JsonCurve->SetNumberField(TEXT("flags"), Curve.GetCurveTypeFlags());
			JsonCurve->SetNumberField(TEXT("key_count"), Curve.FloatCurve.GetNumKeys());
			JsonCurves.Add(MakeShared<FJsonValueObject>(JsonCurve));
		}

		return JsonCurves;
	}

	TArray<TSharedPtr<FJsonValue>> ExportSyncMarkers(const UAnimSequence* Sequence)
	{
		TArray<TSharedPtr<FJsonValue>> JsonMarkers;
		if (!Sequence)
		{
			return JsonMarkers;
		}

		for (int32 MarkerIndex = 0; MarkerIndex < Sequence->AuthoredSyncMarkers.Num(); ++MarkerIndex)
		{
			const FAnimSyncMarker& Marker = Sequence->AuthoredSyncMarkers[MarkerIndex];
			TSharedPtr<FJsonObject> JsonMarker = MakeShared<FJsonObject>();
			JsonMarker->SetNumberField(TEXT("index"), MarkerIndex);
			JsonMarker->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
			JsonMarker->SetNumberField(TEXT("time"), Marker.Time);
#if WITH_EDITORONLY_DATA
			JsonMarker->SetNumberField(TEXT("track_index"), Marker.TrackIndex);
			JsonMarker->SetStringField(TEXT("guid"), Marker.Guid.ToString(EGuidFormats::DigitsWithHyphens));
#endif
			JsonMarkers.Add(MakeShared<FJsonValueObject>(JsonMarker));
		}

		return JsonMarkers;
	}

	TSharedPtr<FJsonObject> ExportSequence(const UAnimSequence* Sequence)
	{
		TSharedPtr<FJsonObject> JsonSequence = MakeShared<FJsonObject>();
		if (!Sequence)
		{
			return JsonSequence;
		}

		JsonSequence->SetNumberField(TEXT("number_of_sampled_keys"), Sequence->GetNumberOfSampledKeys());
		JsonSequence->SetObjectField(TEXT("sampling_frame_rate"), ExportFrameRate(Sequence->GetSamplingFrameRate()));
		JsonSequence->SetBoolField(TEXT("root_motion_enabled"), Sequence->HasRootMotion());
		JsonSequence->SetStringField(TEXT("root_motion_root_lock"), StaticEnum<ERootMotionRootLock::Type>()->GetNameStringByValue(Sequence->RootMotionRootLock.GetValue()));
		JsonSequence->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
		JsonSequence->SetBoolField(TEXT("use_normalized_root_motion_scale"), Sequence->bUseNormalizedRootMotionScale);
		JsonSequence->SetStringField(TEXT("additive_anim_type"), StaticEnum<EAdditiveAnimationType>()->GetNameStringByValue(Sequence->GetAdditiveAnimType()));
		JsonSequence->SetStringField(TEXT("interpolation"), StaticEnum<EAnimInterpolationType>()->GetNameStringByValue(static_cast<int64>(Sequence->Interpolation)));
		JsonSequence->SetArrayField(TEXT("sync_markers"), ExportSyncMarkers(Sequence));
		return JsonSequence;
	}

	TSharedPtr<FJsonObject> ExportBlendParameter(const FBlendParameter& Parameter)
	{
		TSharedPtr<FJsonObject> JsonParameter = MakeShared<FJsonObject>();
		JsonParameter->SetStringField(TEXT("display_name"), Parameter.DisplayName);
		JsonParameter->SetNumberField(TEXT("min"), Parameter.Min);
		JsonParameter->SetNumberField(TEXT("max"), Parameter.Max);
		JsonParameter->SetNumberField(TEXT("grid_num"), Parameter.GridNum);
		JsonParameter->SetBoolField(TEXT("snap_to_grid"), Parameter.bSnapToGrid);
		JsonParameter->SetBoolField(TEXT("wrap_input"), Parameter.bWrapInput);
		return JsonParameter;
	}

	TSharedPtr<FJsonObject> ExportBlendSpace(const UBlendSpace* BlendSpace)
	{
		TSharedPtr<FJsonObject> JsonBlendSpace = MakeShared<FJsonObject>();
		if (!BlendSpace)
		{
			return JsonBlendSpace;
		}

		TArray<TSharedPtr<FJsonValue>> Parameters;
		for (int32 ParameterIndex = 0; ParameterIndex < 3; ++ParameterIndex)
		{
			Parameters.Add(MakeShared<FJsonValueObject>(ExportBlendParameter(BlendSpace->GetBlendParameter(ParameterIndex))));
		}
		JsonBlendSpace->SetArrayField(TEXT("parameters"), Parameters);

		TArray<TSharedPtr<FJsonValue>> Samples;
		const TArray<FBlendSample>& BlendSamples = BlendSpace->GetBlendSamples();
		for (int32 SampleIndex = 0; SampleIndex < BlendSamples.Num(); ++SampleIndex)
		{
			const FBlendSample& Sample = BlendSamples[SampleIndex];
			TSharedPtr<FJsonObject> JsonSample = MakeShared<FJsonObject>();
			JsonSample->SetNumberField(TEXT("index"), SampleIndex);
			JsonSample->SetStringField(TEXT("animation"), ObjectPath(Sample.Animation));
			JsonSample->SetStringField(TEXT("animation_class"), ClassPath(Sample.Animation));
			JsonSample->SetNumberField(TEXT("sample_x"), Sample.SampleValue.X);
			JsonSample->SetNumberField(TEXT("sample_y"), Sample.SampleValue.Y);
			JsonSample->SetNumberField(TEXT("sample_z"), Sample.SampleValue.Z);
			JsonSample->SetNumberField(TEXT("rate_scale"), Sample.RateScale);
			JsonSample->SetBoolField(TEXT("use_single_frame_for_blending"), Sample.bUseSingleFrameForBlending);
			JsonSample->SetNumberField(TEXT("frame_index_to_sample"), Sample.FrameIndexToSample);
			Samples.Add(MakeShared<FJsonValueObject>(JsonSample));
		}
		JsonBlendSpace->SetArrayField(TEXT("samples"), Samples);
		return JsonBlendSpace;
	}

	TArray<TSharedPtr<FJsonValue>> ExportMontageSections(const UAnimMontage* Montage)
	{
		TArray<TSharedPtr<FJsonValue>> Sections;
		if (!Montage)
		{
			return Sections;
		}

		for (int32 SectionIndex = 0; SectionIndex < Montage->CompositeSections.Num(); ++SectionIndex)
		{
			const FCompositeSection& Section = Montage->CompositeSections[SectionIndex];
			TSharedPtr<FJsonObject> JsonSection = MakeShared<FJsonObject>();
			JsonSection->SetNumberField(TEXT("index"), SectionIndex);
			JsonSection->SetStringField(TEXT("name"), Section.SectionName.ToString());
			JsonSection->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
			JsonSection->SetNumberField(TEXT("time"), Section.GetTime());
			Sections.Add(MakeShared<FJsonValueObject>(JsonSection));
		}

		return Sections;
	}

	TArray<TSharedPtr<FJsonValue>> ExportMontageSlots(const UAnimMontage* Montage)
	{
		TArray<TSharedPtr<FJsonValue>> Slots;
		if (!Montage)
		{
			return Slots;
		}

		for (int32 SlotIndex = 0; SlotIndex < Montage->SlotAnimTracks.Num(); ++SlotIndex)
		{
			const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIndex];
			TSharedPtr<FJsonObject> JsonSlot = MakeShared<FJsonObject>();
			JsonSlot->SetNumberField(TEXT("index"), SlotIndex);
			JsonSlot->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());

			TArray<TSharedPtr<FJsonValue>> Segments;
			for (int32 SegmentIndex = 0; SegmentIndex < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegmentIndex)
			{
				const FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegmentIndex];
				TSharedPtr<FJsonObject> JsonSegment = MakeShared<FJsonObject>();
				JsonSegment->SetNumberField(TEXT("index"), SegmentIndex);
				JsonSegment->SetStringField(TEXT("animation"), ObjectPath(Segment.GetAnimReference()));
				JsonSegment->SetStringField(TEXT("animation_class"), ClassPath(Segment.GetAnimReference()));
				JsonSegment->SetNumberField(TEXT("start_pos"), Segment.StartPos);
				JsonSegment->SetNumberField(TEXT("end_pos"), Segment.GetEndPos());
				JsonSegment->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
				JsonSegment->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
				JsonSegment->SetNumberField(TEXT("anim_play_rate"), Segment.AnimPlayRate);
				JsonSegment->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
				Segments.Add(MakeShared<FJsonValueObject>(JsonSegment));
			}
			JsonSlot->SetArrayField(TEXT("segments"), Segments);
			Slots.Add(MakeShared<FJsonValueObject>(JsonSlot));
		}

		return Slots;
	}

	TSharedPtr<FJsonObject> ExportMontage(const UAnimMontage* Montage)
	{
		TSharedPtr<FJsonObject> JsonMontage = MakeShared<FJsonObject>();
		if (!Montage)
		{
			return JsonMontage;
		}

		JsonMontage->SetObjectField(TEXT("sampling_frame_rate"), ExportFrameRate(Montage->GetSamplingFrameRate()));
		JsonMontage->SetNumberField(TEXT("blend_in_time"), Montage->GetDefaultBlendInTime());
		JsonMontage->SetNumberField(TEXT("blend_out_time"), Montage->GetDefaultBlendOutTime());
		JsonMontage->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
		JsonMontage->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);
		JsonMontage->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
		JsonMontage->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
		JsonMontage->SetStringField(TEXT("blend_profile_in"), ObjectPath(Montage->BlendProfileIn));
		JsonMontage->SetStringField(TEXT("blend_profile_out"), ObjectPath(Montage->BlendProfileOut));
		JsonMontage->SetArrayField(TEXT("sections"), ExportMontageSections(Montage));
		JsonMontage->SetArrayField(TEXT("slots"), ExportMontageSlots(Montage));
		return JsonMontage;
	}

	TArray<TSharedPtr<FJsonValue>> ExportReferredAnimationAssets(const UAnimationAsset* AnimationAsset)
	{
		TArray<TSharedPtr<FJsonValue>> Assets;
		if (!AnimationAsset)
		{
			return Assets;
		}

		TArray<UAnimationAsset*> ReferredAssets;
		const_cast<UAnimationAsset*>(AnimationAsset)->GetAllAnimationSequencesReferred(ReferredAssets, true);
		TSet<FString> SeenPaths;
		for (const UAnimationAsset* ReferredAsset : ReferredAssets)
		{
			const FString Path = ObjectPath(ReferredAsset);
			if (Path.IsEmpty() || SeenPaths.Contains(Path))
			{
				continue;
			}

			SeenPaths.Add(Path);
			Assets.Add(MakeShared<FJsonValueObject>(MakeAssetRef(ReferredAsset)));
		}

		return Assets;
	}
}

namespace AssetToJson
{
	bool IsAnimationAsset(const UObject* Asset)
	{
		return Asset && Asset->IsA<UAnimationAsset>();
	}

	TSharedPtr<FJsonObject> ExportAnimationAsset(const UAnimationAsset* AnimationAsset, const FString& ObjectPathString)
	{
		using namespace AssetToJsonAnimationAssetPrivate;

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.asset_to_json.animation_asset.v1"));
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

		TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
		JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
		JsonAsset->SetStringField(TEXT("name"), AnimationAsset ? AnimationAsset->GetName() : FPackageName::ObjectPathToObjectName(ObjectPathString));
		JsonAsset->SetStringField(TEXT("class"), ClassPath(AnimationAsset));
		Root->SetObjectField(TEXT("asset"), JsonAsset);

		TSharedPtr<FJsonObject> JsonAnimation = MakeShared<FJsonObject>();
		if (AnimationAsset)
		{
			JsonAnimation->SetStringField(TEXT("skeleton"), ObjectPath(AnimationAsset->GetSkeleton()));
			JsonAnimation->SetNumberField(TEXT("play_length"), AnimationAsset->GetPlayLength());
			JsonAnimation->SetBoolField(TEXT("valid_additive"), AnimationAsset->IsValidAdditive());
			JsonAnimation->SetArrayField(TEXT("referred_animation_assets"), ExportReferredAnimationAssets(AnimationAsset));

			if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(AnimationAsset))
			{
				JsonAnimation->SetNumberField(TEXT("rate_scale"), SequenceBase->RateScale);
				JsonAnimation->SetBoolField(TEXT("loop"), SequenceBase->bLoop);
				JsonAnimation->SetArrayField(TEXT("notifies"), ExportNotifies(SequenceBase));
				JsonAnimation->SetArrayField(TEXT("curves"), ExportCurves(SequenceBase));
			}

			if (const UAnimSequence* Sequence = Cast<UAnimSequence>(AnimationAsset))
			{
				JsonAnimation->SetObjectField(TEXT("sequence"), ExportSequence(Sequence));
			}

			if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
			{
				JsonAnimation->SetObjectField(TEXT("blend_space"), ExportBlendSpace(BlendSpace));
			}

			if (const UAnimMontage* Montage = Cast<UAnimMontage>(AnimationAsset))
			{
				JsonAnimation->SetObjectField(TEXT("montage"), ExportMontage(Montage));
			}
		}
		Root->SetObjectField(TEXT("animation"), JsonAnimation);
		return Root;
	}
}
