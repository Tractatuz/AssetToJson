#include "AssetToJsonWidgetExport.h"

#include "Animation/WidgetAnimation.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Serialization/JsonTypes.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace AssetToJsonWidgetPrivate
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	bool ShouldExportDetailsProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const bool bIsDetailsVisible = Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
		const bool bIsTransientOnly = Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_TextExportTransient);
		return bIsDetailsVisible && !bIsTransientOnly;
	}

	TSharedPtr<FJsonObject> ExportObjectDefaults(const UObject* Object)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		if (!Object)
		{
			return JsonObject;
		}

		JsonObject->SetStringField(TEXT("object"), ObjectPath(Object));
		JsonObject->SetStringField(TEXT("class"), ObjectPath(Object->GetClass()));

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldExportDetailsProperty(Property))
			{
				continue;
			}

			FString ExportedValue;
			Property->ExportText_InContainer(0, ExportedValue, Object, nullptr, const_cast<UObject*>(Object), PPF_None);

			TSharedPtr<FJsonObject> JsonProperty = MakeShared<FJsonObject>();
			JsonProperty->SetStringField(TEXT("name"), Property->GetName());
			JsonProperty->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
			JsonProperty->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
			JsonProperty->SetStringField(TEXT("owner_class"), ObjectPath(Property->GetOwnerClass()));
			JsonProperty->SetStringField(TEXT("value"), ExportedValue);
			Properties.Add(MakeShared<FJsonValueObject>(JsonProperty));
		}

		JsonObject->SetArrayField(TEXT("properties"), Properties);
		return JsonObject;
	}

	TSharedPtr<FJsonObject> ExportWidgetAnimationBase(const UWidgetAnimation* Animation)
	{
		TSharedPtr<FJsonObject> JsonAnimation = MakeShared<FJsonObject>();
		if (!Animation)
		{
			return JsonAnimation;
		}

		JsonAnimation->SetStringField(TEXT("name"), Animation->GetName());
		JsonAnimation->SetStringField(TEXT("path"), ObjectPath(Animation));
		JsonAnimation->SetStringField(TEXT("class"), ClassPath(Animation));
		JsonAnimation->SetStringField(TEXT("display_label"), Animation->GetDisplayLabel());
		JsonAnimation->SetStringField(TEXT("movie_scene"), ObjectPath(Animation->GetMovieScene()));
		JsonAnimation->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Animation));
		return JsonAnimation;
	}

	TSharedPtr<FJsonObject> ExportFrameNumber(const FFrameNumber& FrameNumber, const FFrameRate& TickResolution)
	{
		TSharedPtr<FJsonObject> JsonFrame = MakeShared<FJsonObject>();
		JsonFrame->SetNumberField(TEXT("frame"), FrameNumber.Value);
		JsonFrame->SetNumberField(TEXT("seconds"), TickResolution.AsSeconds(FrameNumber));
		return JsonFrame;
	}

	TSharedPtr<FJsonObject> ExportFrameRange(const TRange<FFrameNumber>& Range, const FFrameRate& TickResolution)
	{
		TSharedPtr<FJsonObject> JsonRange = MakeShared<FJsonObject>();
		if (Range.HasLowerBound())
		{
			JsonRange->SetObjectField(TEXT("start"), ExportFrameNumber(Range.GetLowerBoundValue(), TickResolution));
		}

		if (Range.HasUpperBound())
		{
			JsonRange->SetObjectField(TEXT("end"), ExportFrameNumber(Range.GetUpperBoundValue(), TickResolution));
		}

		return JsonRange;
	}

	template <typename ChannelType, typename KeyValueExporterType, typename DefaultValueExporterType>
	void AddMovieSceneChannels(const FMovieSceneChannelProxy& ChannelProxy, const FString& TypeName, KeyValueExporterType ExportKeyValue, DefaultValueExporterType ExportDefaultValue, TArray<TSharedPtr<FJsonValue>>& Channels)
	{
		TArrayView<ChannelType*> TypedChannels = ChannelProxy.GetChannels<ChannelType>();
		TArrayView<const FMovieSceneChannelMetaData> MetaData = ChannelProxy.GetMetaData<ChannelType>();
		for (int32 ChannelIndex = 0; ChannelIndex < TypedChannels.Num(); ++ChannelIndex)
		{
			const ChannelType* Channel = TypedChannels[ChannelIndex];
			if (!Channel)
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonChannel = MakeShared<FJsonObject>();
			JsonChannel->SetStringField(TEXT("type"), TypeName);
			JsonChannel->SetNumberField(TEXT("index"), ChannelIndex);
			if (MetaData.IsValidIndex(ChannelIndex))
			{
				JsonChannel->SetStringField(TEXT("name"), MetaData[ChannelIndex].Name.ToString());
				JsonChannel->SetStringField(TEXT("display_text"), MetaData[ChannelIndex].DisplayText.ToString());
			}

			const auto DefaultValue = Channel->GetDefault();
			if (DefaultValue.IsSet())
			{
				JsonChannel->SetField(TEXT("default_value"), ExportDefaultValue(DefaultValue.GetValue()));
			}

			TArray<TSharedPtr<FJsonValue>> Keys;
			const auto Times = Channel->GetTimes();
			const auto Values = Channel->GetValues();
			const int32 KeyCount = FMath::Min(Times.Num(), Values.Num());
			for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
			{
				TSharedPtr<FJsonObject> JsonKey = MakeShared<FJsonObject>();
				JsonKey->SetNumberField(TEXT("frame"), Times[KeyIndex].Value);
				JsonKey->SetField(TEXT("value"), ExportKeyValue(Values[KeyIndex]));
				Keys.Add(MakeShared<FJsonValueObject>(JsonKey));
			}
			JsonChannel->SetArrayField(TEXT("keys"), Keys);
			Channels.Add(MakeShared<FJsonValueObject>(JsonChannel));
		}
	}

	TSharedPtr<FJsonValue> NumberJsonValue(double Value)
	{
		return MakeShared<FJsonValueNumber>(Value);
	}

	TSharedPtr<FJsonValue> BoolJsonValue(bool bValue)
	{
		return MakeShared<FJsonValueBoolean>(bValue);
	}

	TSharedPtr<FJsonObject> ExportMovieSceneSection(const UMovieSceneSection* Section, const FFrameRate& TickResolution)
	{
		TSharedPtr<FJsonObject> JsonSection = MakeShared<FJsonObject>();
		if (!Section)
		{
			return JsonSection;
		}

		JsonSection->SetStringField(TEXT("name"), Section->GetName());
		JsonSection->SetStringField(TEXT("class"), ClassPath(Section));
		JsonSection->SetNumberField(TEXT("row_index"), Section->GetRowIndex());
		JsonSection->SetBoolField(TEXT("active"), Section->IsActive());
		JsonSection->SetBoolField(TEXT("locked"), Section->IsLocked());
		JsonSection->SetObjectField(TEXT("range"), ExportFrameRange(Section->GetRange(), TickResolution));
		JsonSection->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Section));

		TArray<TSharedPtr<FJsonValue>> Channels;
		const FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
		AddMovieSceneChannels<FMovieSceneFloatChannel>(ChannelProxy, TEXT("float"), [](const FMovieSceneFloatValue& Value) { return NumberJsonValue(Value.Value); }, [](float Value) { return NumberJsonValue(Value); }, Channels);
		AddMovieSceneChannels<FMovieSceneDoubleChannel>(ChannelProxy, TEXT("double"), [](const FMovieSceneDoubleValue& Value) { return NumberJsonValue(Value.Value); }, [](double Value) { return NumberJsonValue(Value); }, Channels);
		AddMovieSceneChannels<FMovieSceneBoolChannel>(ChannelProxy, TEXT("bool"), [](bool bValue) { return BoolJsonValue(bValue); }, [](bool bValue) { return BoolJsonValue(bValue); }, Channels);
		AddMovieSceneChannels<FMovieSceneIntegerChannel>(ChannelProxy, TEXT("integer"), [](int32 Value) { return NumberJsonValue(Value); }, [](int32 Value) { return NumberJsonValue(Value); }, Channels);
		AddMovieSceneChannels<FMovieSceneByteChannel>(ChannelProxy, TEXT("byte"), [](uint8 Value) { return NumberJsonValue(Value); }, [](uint8 Value) { return NumberJsonValue(Value); }, Channels);
		JsonSection->SetArrayField(TEXT("channels"), Channels);

		return JsonSection;
	}

	TSharedPtr<FJsonObject> ExportMovieSceneTrack(const UMovieSceneTrack* Track, const FFrameRate& TickResolution)
	{
		TSharedPtr<FJsonObject> JsonTrack = MakeShared<FJsonObject>();
		if (!Track)
		{
			return JsonTrack;
		}

		JsonTrack->SetStringField(TEXT("name"), Track->GetName());
		JsonTrack->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());
		JsonTrack->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());
		JsonTrack->SetStringField(TEXT("class"), ClassPath(Track));
		JsonTrack->SetObjectField(TEXT("defaults"), ExportObjectDefaults(Track));

		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const UMovieSceneSection* Section : Track->GetAllSections())
		{
			Sections.Add(MakeShared<FJsonValueObject>(ExportMovieSceneSection(Section, TickResolution)));
		}
		JsonTrack->SetArrayField(TEXT("sections"), Sections);

		return JsonTrack;
	}

	TSharedPtr<FJsonObject> ExportMovieScene(const UMovieScene* MovieScene)
	{
		TSharedPtr<FJsonObject> JsonMovieScene = MakeShared<FJsonObject>();
		if (!MovieScene)
		{
			return JsonMovieScene;
		}

		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		JsonMovieScene->SetStringField(TEXT("path"), ObjectPath(MovieScene));
		JsonMovieScene->SetStringField(TEXT("class"), ClassPath(MovieScene));
		JsonMovieScene->SetStringField(TEXT("name"), MovieScene->GetName());
		JsonMovieScene->SetNumberField(TEXT("tick_resolution_numerator"), TickResolution.Numerator);
		JsonMovieScene->SetNumberField(TEXT("tick_resolution_denominator"), TickResolution.Denominator);
		JsonMovieScene->SetObjectField(TEXT("playback_range"), ExportFrameRange(MovieScene->GetPlaybackRange(), TickResolution));

		TArray<TSharedPtr<FJsonValue>> MasterTracks;
		for (const UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			MasterTracks.Add(MakeShared<FJsonValueObject>(ExportMovieSceneTrack(Track, TickResolution)));
		}
		JsonMovieScene->SetArrayField(TEXT("tracks"), MasterTracks);

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			TSharedPtr<FJsonObject> JsonBinding = MakeShared<FJsonObject>();
			JsonBinding->SetStringField(TEXT("guid"), GuidToString(Binding.GetObjectGuid()));
			TArray<TSharedPtr<FJsonValue>> BindingTracks;
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				BindingTracks.Add(MakeShared<FJsonValueObject>(ExportMovieSceneTrack(Track, TickResolution)));
			}
			JsonBinding->SetArrayField(TEXT("tracks"), BindingTracks);
			Bindings.Add(MakeShared<FJsonValueObject>(JsonBinding));
		}
		JsonMovieScene->SetArrayField(TEXT("bindings"), Bindings);

		return JsonMovieScene;
	}
}

namespace AssetToJson
{
	TSharedPtr<FJsonObject> ExportEnhancedWidgetAnimation(const UWidgetAnimation* Animation)
	{
		TSharedPtr<FJsonObject> JsonAnimation = AssetToJsonWidgetPrivate::ExportWidgetAnimationBase(Animation);
		if (Animation)
		{
			JsonAnimation->SetObjectField(TEXT("movie_scene_data"), AssetToJsonWidgetPrivate::ExportMovieScene(Animation->GetMovieScene()));
		}

		return JsonAnimation;
	}

	TSharedPtr<FJsonObject> ExportWidgetBinding(const FDelegateEditorBinding& Binding)
	{
		TSharedPtr<FJsonObject> JsonBinding = MakeShared<FJsonObject>();
		JsonBinding->SetStringField(TEXT("object_name"), Binding.ObjectName);
		JsonBinding->SetStringField(TEXT("property_name"), Binding.PropertyName.ToString());
		JsonBinding->SetStringField(TEXT("function_name"), Binding.FunctionName.ToString());
		JsonBinding->SetStringField(TEXT("source_property"), Binding.SourceProperty.ToString());
		JsonBinding->SetStringField(TEXT("member_guid"), AssetToJsonWidgetPrivate::GuidToString(Binding.MemberGuid));
		JsonBinding->SetStringField(TEXT("kind"), StaticEnum<EBindingKind>()->GetNameStringByValue(static_cast<int64>(Binding.Kind)));
		return JsonBinding;
	}
}
