#include "AssetToJsonLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString ClassPath(const UObject* Object)
	{
		return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString();
	}

	FString NormalizeObjectPath(const FString& AssetPath)
	{
		const FString TrimmedPath = AssetPath.TrimStartAndEnd();

		if (!TrimmedPath.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(TrimmedPath))
		{
			return FString::Printf(TEXT("%s.%s"), *TrimmedPath, *FPackageName::GetLongPackageAssetName(TrimmedPath));
		}

		return TrimmedPath;
	}

	TSharedPtr<FJsonObject> MakeErrorJson(const FString& Message)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("ue.blueprint.visual_script.v1"));
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), Message);
		return Root;
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Root, bool bPrettyPrint)
	{
		FString JsonString;

		if (bPrettyPrint)
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
			FJsonSerializer::Serialize(Root, Writer);
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
			FJsonSerializer::Serialize(Root, Writer);
		}

		return JsonString;
	}

	FString ContainerTypeToString(EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array:
			return TEXT("array");
		case EPinContainerType::Set:
			return TEXT("set");
		case EPinContainerType::Map:
			return TEXT("map");
		case EPinContainerType::None:
		default:
			return TEXT("none");
		}
	}

	FString PinDirectionToString(EEdGraphPinDirection Direction)
	{
		switch (Direction)
		{
		case EGPD_Input:
			return TEXT("input");
		case EGPD_Output:
			return TEXT("output");
		default:
			return TEXT("unknown");
		}
	}

	FString PinIdToString(const UEdGraphPin* Pin)
	{
		return Pin ? Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	FString NodeIdToString(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	TSharedPtr<FJsonObject> ExportPinType(const FEdGraphPinType& PinType)
	{
		TSharedPtr<FJsonObject> JsonPinType = MakeShared<FJsonObject>();
		JsonPinType->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
		JsonPinType->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
		JsonPinType->SetStringField(TEXT("subcategory_object"), ObjectPath(PinType.PinSubCategoryObject.Get()));
		JsonPinType->SetStringField(TEXT("container"), ContainerTypeToString(PinType.ContainerType));
		JsonPinType->SetBoolField(TEXT("is_reference"), PinType.bIsReference);
		JsonPinType->SetBoolField(TEXT("is_const"), PinType.bIsConst);
		JsonPinType->SetBoolField(TEXT("is_weak_pointer"), PinType.bIsWeakPointer);
		JsonPinType->SetBoolField(TEXT("is_uobject_wrapper"), PinType.bIsUObjectWrapper);
		return JsonPinType;
	}

	TSharedPtr<FJsonObject> ExportPin(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> JsonPin = MakeShared<FJsonObject>();
		if (!Pin)
		{
			return JsonPin;
		}

		JsonPin->SetStringField(TEXT("id"), PinIdToString(Pin));
		JsonPin->SetStringField(TEXT("name"), Pin->PinName.ToString());
		JsonPin->SetStringField(TEXT("display_name"), Pin->GetDisplayName().ToString());
		JsonPin->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
		JsonPin->SetObjectField(TEXT("type"), ExportPinType(Pin->PinType));
		JsonPin->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		JsonPin->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
		JsonPin->SetStringField(TEXT("default_object"), ObjectPath(Pin->DefaultObject));
		JsonPin->SetBoolField(TEXT("hidden"), Pin->bHidden);
		JsonPin->SetBoolField(TEXT("not_connectable"), Pin->bNotConnectable);

		TArray<TSharedPtr<FJsonValue>> LinkedTo;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode())
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonLink = MakeShared<FJsonObject>();
			JsonLink->SetStringField(TEXT("node_id"), NodeIdToString(LinkedPin->GetOwningNode()));
			JsonLink->SetStringField(TEXT("pin_id"), PinIdToString(LinkedPin));
			JsonLink->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
			LinkedTo.Add(MakeShared<FJsonValueObject>(JsonLink));
		}

		JsonPin->SetArrayField(TEXT("linked_to"), LinkedTo);
		return JsonPin;
	}

	void AddNodeProperties(const UEdGraphNode* Node, const TSharedPtr<FJsonObject>& JsonNode)
	{
		static const TSet<FName> SkippedProperties = {
			TEXT("Pins"),
			TEXT("NodeGuid"),
			TEXT("NodePosX"),
			TEXT("NodePosY"),
			TEXT("NodeWidth"),
			TEXT("NodeHeight"),
			TEXT("NodeComment"),
			TEXT("ErrorMsg"),
			TEXT("ErrorType")
		};

		TSharedPtr<FJsonObject> JsonProperties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || SkippedProperties.Contains(Property->GetFName()))
			{
				continue;
			}

			FString ExportedValue;
			Property->ExportText_InContainer(0, ExportedValue, Node, Node, const_cast<UEdGraphNode*>(Node), PPF_None);
			JsonProperties->SetStringField(Property->GetName(), ExportedValue);
		}

		JsonNode->SetObjectField(TEXT("properties"), JsonProperties);
	}

	TSharedPtr<FJsonObject> ExportNode(const UEdGraphNode* Node, bool bIncludeNodeProperties)
	{
		TSharedPtr<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		if (!Node)
		{
			return JsonNode;
		}

		JsonNode->SetStringField(TEXT("id"), NodeIdToString(Node));
		JsonNode->SetStringField(TEXT("name"), Node->GetName());
		JsonNode->SetStringField(TEXT("class"), ClassPath(Node));
		JsonNode->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		JsonNode->SetStringField(TEXT("comment"), Node->NodeComment);

		TSharedPtr<FJsonObject> JsonPosition = MakeShared<FJsonObject>();
		JsonPosition->SetNumberField(TEXT("x"), Node->NodePosX);
		JsonPosition->SetNumberField(TEXT("y"), Node->NodePosY);
		JsonNode->SetObjectField(TEXT("position"), JsonPosition);

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			Pins.Add(MakeShared<FJsonValueObject>(ExportPin(Pin)));
		}
		JsonNode->SetArrayField(TEXT("pins"), Pins);

		if (bIncludeNodeProperties)
		{
			AddNodeProperties(Node, JsonNode);
		}

		return JsonNode;
	}

	TSharedPtr<FJsonObject> ExportLink(const UEdGraphNode* FromNode, const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
	{
		TSharedPtr<FJsonObject> JsonLink = MakeShared<FJsonObject>();
		if (!FromNode || !FromPin || !ToPin || !ToPin->GetOwningNode())
		{
			return JsonLink;
		}

		TSharedPtr<FJsonObject> JsonFrom = MakeShared<FJsonObject>();
		JsonFrom->SetStringField(TEXT("node_id"), NodeIdToString(FromNode));
		JsonFrom->SetStringField(TEXT("pin_id"), PinIdToString(FromPin));
		JsonFrom->SetStringField(TEXT("pin_name"), FromPin->PinName.ToString());

		TSharedPtr<FJsonObject> JsonTo = MakeShared<FJsonObject>();
		JsonTo->SetStringField(TEXT("node_id"), NodeIdToString(ToPin->GetOwningNode()));
		JsonTo->SetStringField(TEXT("pin_id"), PinIdToString(ToPin));
		JsonTo->SetStringField(TEXT("pin_name"), ToPin->PinName.ToString());

		JsonLink->SetObjectField(TEXT("from"), JsonFrom);
		JsonLink->SetObjectField(TEXT("to"), JsonTo);
		return JsonLink;
	}

	TSharedPtr<FJsonObject> ExportGraph(const UEdGraph* Graph, const FString& GraphType, bool bIncludeT3DText, bool bIncludeNodeProperties)
	{
		TSharedPtr<FJsonObject> JsonGraph = MakeShared<FJsonObject>();
		if (!Graph)
		{
			return JsonGraph;
		}

		JsonGraph->SetStringField(TEXT("name"), Graph->GetName());
		JsonGraph->SetStringField(TEXT("type"), GraphType);
		JsonGraph->SetStringField(TEXT("schema"), ClassPath(Graph->GetSchema()));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		TArray<TSharedPtr<FJsonValue>> Links;
		TSet<FString> SeenLinks;

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			Nodes.Add(MakeShared<FJsonValueObject>(ExportNode(Node, bIncludeNodeProperties)));

			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}

				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}

					const bool bUseThisDirection = Pin->Direction == EGPD_Output || LinkedPin->Direction != EGPD_Output;
					if (!bUseThisDirection)
					{
						continue;
					}

					const FString LinkKey = FString::Printf(TEXT("%s:%s>%s:%s"), *NodeIdToString(Node), *PinIdToString(Pin), *NodeIdToString(LinkedPin->GetOwningNode()), *PinIdToString(LinkedPin));
					if (SeenLinks.Contains(LinkKey))
					{
						continue;
					}

					SeenLinks.Add(LinkKey);
					Links.Add(MakeShared<FJsonValueObject>(ExportLink(Node, Pin, LinkedPin)));
				}
			}
		}

		JsonGraph->SetArrayField(TEXT("nodes"), Nodes);
		JsonGraph->SetArrayField(TEXT("links"), Links);

		if (bIncludeT3DText)
		{
			TSet<UObject*> NodesToExport;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node)
				{
					NodesToExport.Add(Node);
				}
			}

			FString ExportedText;
			FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);
			JsonGraph->SetStringField(TEXT("t3d_text"), ExportedText);
		}

		return JsonGraph;
	}

	void AddGraphs(const TArray<UEdGraph*>& Graphs, const FString& GraphType, bool bIncludeT3DText, bool bIncludeNodeProperties, TArray<TSharedPtr<FJsonValue>>& JsonGraphs)
	{
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph)
			{
				JsonGraphs.Add(MakeShared<FJsonValueObject>(ExportGraph(Graph, GraphType, bIncludeT3DText, bIncludeNodeProperties)));
			}
		}
	}

	TSharedPtr<FJsonObject> ExportVariable(const FBPVariableDescription& Variable)
	{
		TSharedPtr<FJsonObject> JsonVariable = MakeShared<FJsonObject>();
		JsonVariable->SetStringField(TEXT("name"), Variable.VarName.ToString());
		JsonVariable->SetStringField(TEXT("guid"), Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
		JsonVariable->SetStringField(TEXT("friendly_name"), Variable.FriendlyName);
		JsonVariable->SetStringField(TEXT("category"), Variable.Category.ToString());
		JsonVariable->SetStringField(TEXT("default_value"), Variable.DefaultValue);
		JsonVariable->SetStringField(TEXT("rep_notify_func"), Variable.RepNotifyFunc.ToString());
		JsonVariable->SetNumberField(TEXT("property_flags"), static_cast<double>(Variable.PropertyFlags));
		JsonVariable->SetObjectField(TEXT("type"), ExportPinType(Variable.VarType));
		return JsonVariable;
	}

	TSharedPtr<FJsonObject> ExportSCSNode(const USCS_Node* Node)
	{
		TSharedPtr<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		if (!Node)
		{
			return JsonNode;
		}

		JsonNode->SetStringField(TEXT("variable_name"), Node->GetVariableName().ToString());
		JsonNode->SetStringField(TEXT("component_class"), ObjectPath(Node->ComponentClass));
		JsonNode->SetStringField(TEXT("component_template"), ObjectPath(Node->ComponentTemplate));

		TArray<TSharedPtr<FJsonValue>> Children;
		for (const USCS_Node* ChildNode : Node->GetChildNodes())
		{
			Children.Add(MakeShared<FJsonValueObject>(ExportSCSNode(ChildNode)));
		}
		JsonNode->SetArrayField(TEXT("children"), Children);

		return JsonNode;
	}
}

FString UAssetToJsonLibrary::ReadBlueprintVisualScriptAsJson(
	const FString& AssetPath,
	const FString& OutputFilePath,
	bool bPrettyPrint,
	bool bReturnJson,
	bool bIncludeT3DText,
	bool bIncludeNodeProperties)
{
	const FString ObjectPathString = NormalizeObjectPath(AssetPath);
	if (ObjectPathString.IsEmpty())
	{
		return SerializeJson(MakeErrorJson(TEXT("AssetPath is empty")).ToSharedRef(), bPrettyPrint);
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPathString);
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return SerializeJson(MakeErrorJson(FString::Printf(TEXT("Asset is not a Blueprint or could not be loaded: %s"), *ObjectPathString)).ToSharedRef(), bPrettyPrint);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("ue.blueprint.visual_script.v1"));
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

	TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
	JsonAsset->SetStringField(TEXT("path"), FPackageName::ObjectPathToPackageName(ObjectPathString));
	JsonAsset->SetStringField(TEXT("object_path"), ObjectPathString);
	JsonAsset->SetStringField(TEXT("name"), Blueprint->GetName());
	JsonAsset->SetStringField(TEXT("class"), ClassPath(Blueprint));
	JsonAsset->SetStringField(TEXT("parent_class"), ObjectPath(Blueprint->ParentClass));
	JsonAsset->SetStringField(TEXT("generated_class"), ObjectPath(Blueprint->GeneratedClass));
	Root->SetObjectField(TEXT("asset"), JsonAsset);

	TSharedPtr<FJsonObject> JsonBlueprint = MakeShared<FJsonObject>();
	JsonBlueprint->SetStringField(TEXT("type"), StaticEnum<EBlueprintType>()->GetNameStringByValue(Blueprint->BlueprintType));
	JsonBlueprint->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Blueprint->Status));
	Root->SetObjectField(TEXT("blueprint"), JsonBlueprint);

	TArray<TSharedPtr<FJsonValue>> Graphs;
	AddGraphs(Blueprint->UbergraphPages, TEXT("ubergraph"), bIncludeT3DText, bIncludeNodeProperties, Graphs);
	AddGraphs(Blueprint->FunctionGraphs, TEXT("function"), bIncludeT3DText, bIncludeNodeProperties, Graphs);
	AddGraphs(Blueprint->MacroGraphs, TEXT("macro"), bIncludeT3DText, bIncludeNodeProperties, Graphs);
	AddGraphs(Blueprint->DelegateSignatureGraphs, TEXT("delegate_signature"), bIncludeT3DText, bIncludeNodeProperties, Graphs);
	Root->SetArrayField(TEXT("graphs"), Graphs);

	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		Variables.Add(MakeShared<FJsonValueObject>(ExportVariable(Variable)));
	}
	Root->SetArrayField(TEXT("variables"), Variables);

	TArray<TSharedPtr<FJsonValue>> Components;
	if (Blueprint->SimpleConstructionScript)
	{
		for (const USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
		{
			Components.Add(MakeShared<FJsonValueObject>(ExportSCSNode(RootNode)));
		}
	}
	Root->SetArrayField(TEXT("components"), Components);

	const FString JsonString = SerializeJson(Root.ToSharedRef(), bPrettyPrint);
	if (!OutputFilePath.IsEmpty())
	{
		const FString Directory = FPaths::GetPath(OutputFilePath);
		if (!Directory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
		}

		if (!FFileHelper::SaveStringToFile(JsonString, *OutputFilePath))
		{
			return SerializeJson(MakeErrorJson(FString::Printf(TEXT("Failed to write JSON file: %s"), *OutputFilePath)).ToSharedRef(), bPrettyPrint);
		}
	}

	return bReturnJson ? JsonString : FString();
}
