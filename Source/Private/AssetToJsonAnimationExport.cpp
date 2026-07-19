#include "AssetToJsonAnimationExport.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Serialization/JsonTypes.h"

namespace AssetToJsonAnimationPrivate
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

	FString NodeIdToString(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
	}

	TSharedPtr<FJsonObject> ExportAnimStateNode(const UAnimStateNode* StateNode)
	{
		TSharedPtr<FJsonObject> JsonState = MakeShared<FJsonObject>();
		if (!StateNode)
		{
			return JsonState;
		}

		JsonState->SetStringField(TEXT("id"), NodeIdToString(StateNode));
		JsonState->SetStringField(TEXT("name"), StateNode->GetStateName());
		JsonState->SetStringField(TEXT("class"), ClassPath(StateNode));
		JsonState->SetStringField(TEXT("bound_graph"), ObjectPath(StateNode->BoundGraph));
		JsonState->SetStringField(TEXT("state_type"), StaticEnum<EAnimStateType>()->GetNameStringByValue(StateNode->StateType.GetValue()));
		JsonState->SetBoolField(TEXT("always_reset_on_entry"), StateNode->bAlwaysResetOnEntry);
		return JsonState;
	}

	TSharedPtr<FJsonObject> ExportAnimTransitionNode(const UAnimStateTransitionNode* TransitionNode)
	{
		TSharedPtr<FJsonObject> JsonTransition = MakeShared<FJsonObject>();
		if (!TransitionNode)
		{
			return JsonTransition;
		}

		JsonTransition->SetStringField(TEXT("id"), NodeIdToString(TransitionNode));
		JsonTransition->SetStringField(TEXT("name"), TransitionNode->GetStateName());
		JsonTransition->SetStringField(TEXT("class"), ClassPath(TransitionNode));
		JsonTransition->SetStringField(TEXT("bound_graph"), ObjectPath(TransitionNode->BoundGraph));
		JsonTransition->SetStringField(TEXT("custom_transition_graph"), ObjectPath(TransitionNode->CustomTransitionGraph));
		JsonTransition->SetStringField(TEXT("previous_state"), TransitionNode->GetPreviousState() ? TransitionNode->GetPreviousState()->GetStateName() : FString());
		JsonTransition->SetStringField(TEXT("next_state"), TransitionNode->GetNextState() ? TransitionNode->GetNextState()->GetStateName() : FString());
		JsonTransition->SetNumberField(TEXT("priority_order"), TransitionNode->PriorityOrder);
		JsonTransition->SetNumberField(TEXT("crossfade_duration"), TransitionNode->CrossfadeDuration);
		JsonTransition->SetStringField(TEXT("blend_mode"), StaticEnum<EAlphaBlendOption>()->GetNameStringByValue(static_cast<int64>(TransitionNode->BlendMode)));
		JsonTransition->SetBoolField(TEXT("automatic_rule_based_on_sequence_player_in_state"), TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState);
		JsonTransition->SetNumberField(TEXT("automatic_rule_trigger_time"), TransitionNode->AutomaticRuleTriggerTime);
		JsonTransition->SetNumberField(TEXT("min_time_before_reentry"), TransitionNode->MinTimeBeforeReentry);
		JsonTransition->SetStringField(TEXT("sync_group_name_to_require_valid_markers_rule"), TransitionNode->SyncGroupNameToRequireValidMarkersRule.ToString());
		JsonTransition->SetStringField(TEXT("logic_type"), StaticEnum<ETransitionLogicType::Type>()->GetNameStringByValue(TransitionNode->LogicType.GetValue()));
		JsonTransition->SetBoolField(TEXT("bidirectional"), TransitionNode->Bidirectional);
		JsonTransition->SetBoolField(TEXT("disabled"), TransitionNode->bDisabled);
		JsonTransition->SetBoolField(TEXT("shared_rules"), TransitionNode->bSharedRules);
		JsonTransition->SetStringField(TEXT("shared_rules_name"), TransitionNode->SharedRulesName);
		JsonTransition->SetStringField(TEXT("shared_rules_guid"), GuidToString(TransitionNode->SharedRulesGuid));
		JsonTransition->SetBoolField(TEXT("shared_crossfade"), TransitionNode->bSharedCrossfade);
		JsonTransition->SetStringField(TEXT("shared_crossfade_name"), TransitionNode->SharedCrossfadeName);
		JsonTransition->SetStringField(TEXT("shared_crossfade_guid"), GuidToString(TransitionNode->SharedCrossfadeGuid));
		return JsonTransition;
	}

	TSharedPtr<FJsonObject> ExportAnimationStateMachine(const UAnimationStateMachineGraph* StateMachineGraph)
	{
		TSharedPtr<FJsonObject> JsonStateMachine = MakeShared<FJsonObject>();
		if (!StateMachineGraph)
		{
			return JsonStateMachine;
		}

		JsonStateMachine->SetStringField(TEXT("name"), StateMachineGraph->GetName());
		JsonStateMachine->SetStringField(TEXT("path"), ObjectPath(StateMachineGraph));
		JsonStateMachine->SetStringField(TEXT("entry_node"), ObjectPath(StateMachineGraph->EntryNode.Get()));
		JsonStateMachine->SetStringField(TEXT("owner_node"), ObjectPath(StateMachineGraph->OwnerAnimGraphNode.Get()));

		TArray<TSharedPtr<FJsonValue>> States;
		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (const UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				States.Add(MakeShared<FJsonValueObject>(ExportAnimStateNode(StateNode)));
			}
			else if (const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
			{
				Transitions.Add(MakeShared<FJsonValueObject>(ExportAnimTransitionNode(TransitionNode)));
			}
		}
		JsonStateMachine->SetArrayField(TEXT("states"), States);
		JsonStateMachine->SetArrayField(TEXT("transitions"), Transitions);

		return JsonStateMachine;
	}

	TSharedPtr<FJsonObject> ExportLinkedInputPose(const UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode)
	{
		TSharedPtr<FJsonObject> JsonLinkedInputPose = MakeShared<FJsonObject>();
		if (!LinkedInputPoseNode)
		{
			return JsonLinkedInputPose;
		}

		JsonLinkedInputPose->SetStringField(TEXT("kind"), TEXT("linked_input_pose"));
		JsonLinkedInputPose->SetNumberField(TEXT("input_pose_index"), LinkedInputPoseNode->InputPoseIndex);
		JsonLinkedInputPose->SetStringField(TEXT("function_name"), LinkedInputPoseNode->FunctionReference.GetMemberName().ToString());
		JsonLinkedInputPose->SetStringField(TEXT("function_parent_class"), ObjectPath(LinkedInputPoseNode->FunctionReference.GetMemberParentClass()));

		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (const FAnimBlueprintFunctionPinInfo& Input : LinkedInputPoseNode->Inputs)
		{
			TSharedPtr<FJsonObject> JsonInput = MakeShared<FJsonObject>();
			JsonInput->SetStringField(TEXT("name"), Input.Name.ToString());
			JsonInput->SetStringField(TEXT("category"), Input.Type.PinCategory.ToString());
			JsonInput->SetStringField(TEXT("subcategory"), Input.Type.PinSubCategory.ToString());
			JsonInput->SetStringField(TEXT("subcategory_object"), ObjectPath(Input.Type.PinSubCategoryObject.Get()));
			Inputs.Add(MakeShared<FJsonValueObject>(JsonInput));
		}
		JsonLinkedInputPose->SetArrayField(TEXT("inputs"), Inputs);
		return JsonLinkedInputPose;
	}

	TSharedPtr<FJsonObject> ExportLinkedAnimGraph(const UAnimGraphNode_LinkedAnimGraph* LinkedAnimGraphNode)
	{
		TSharedPtr<FJsonObject> JsonLinkedGraph = MakeShared<FJsonObject>();
		if (!LinkedAnimGraphNode)
		{
			return JsonLinkedGraph;
		}

		JsonLinkedGraph->SetStringField(TEXT("kind"), TEXT("linked_anim_graph"));
		JsonLinkedGraph->SetStringField(TEXT("instance_class"), ObjectPath(LinkedAnimGraphNode->Node.InstanceClass));
		JsonLinkedGraph->SetBoolField(TEXT("receive_notifies_from_linked_instances"), LinkedAnimGraphNode->Node.bReceiveNotifiesFromLinkedInstances);
		JsonLinkedGraph->SetBoolField(TEXT("propagate_notifies_to_linked_instances"), LinkedAnimGraphNode->Node.bPropagateNotifiesToLinkedInstances);
		return JsonLinkedGraph;
	}

	TSharedPtr<FJsonObject> ExportLinkedAnimLayer(const UAnimGraphNode_LinkedAnimLayer* LinkedAnimLayerNode)
	{
		TSharedPtr<FJsonObject> JsonLinkedLayer = MakeShared<FJsonObject>();
		if (!LinkedAnimLayerNode)
		{
			return JsonLinkedLayer;
		}

		JsonLinkedLayer->SetStringField(TEXT("kind"), TEXT("linked_anim_layer"));
		JsonLinkedLayer->SetStringField(TEXT("instance_class"), ObjectPath(LinkedAnimLayerNode->Node.InstanceClass));
		JsonLinkedLayer->SetStringField(TEXT("layer_name"), LinkedAnimLayerNode->Node.Layer.ToString());
		JsonLinkedLayer->SetStringField(TEXT("interface_guid"), GuidToString(LinkedAnimLayerNode->InterfaceGuid));
		JsonLinkedLayer->SetStringField(TEXT("interface"), ObjectPath(LinkedAnimLayerNode->Node.Interface));
		JsonLinkedLayer->SetBoolField(TEXT("receive_notifies_from_linked_instances"), LinkedAnimLayerNode->Node.bReceiveNotifiesFromLinkedInstances);
		JsonLinkedLayer->SetBoolField(TEXT("propagate_notifies_to_linked_instances"), LinkedAnimLayerNode->Node.bPropagateNotifiesToLinkedInstances);
		return JsonLinkedLayer;
	}
}

namespace AssetToJson
{
	TArray<TSharedPtr<FJsonValue>> ExportAnimationStateMachines(const UAnimBlueprint* AnimBlueprint)
	{
		TArray<TSharedPtr<FJsonValue>> StateMachines;
		if (!AnimBlueprint)
		{
			return StateMachines;
		}

		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (const UEdGraph* Graph : AllGraphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (const UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
				{
					StateMachines.Add(MakeShared<FJsonValueObject>(AssetToJsonAnimationPrivate::ExportAnimationStateMachine(StateMachineNode->EditorStateMachineGraph)));
				}
			}
		}

		return StateMachines;
	}

	TSharedPtr<FJsonObject> ExportAnimGraphNodeDetails(const UAnimGraphNode_Base* AnimGraphNode)
	{
		TSharedPtr<FJsonObject> JsonAnimNode = MakeShared<FJsonObject>();
		if (!AnimGraphNode)
		{
			return JsonAnimNode;
		}

		JsonAnimNode->SetStringField(TEXT("kind"), TEXT("anim_graph_node"));
		JsonAnimNode->SetStringField(TEXT("node_class"), AssetToJsonAnimationPrivate::ClassPath(AnimGraphNode));
		JsonAnimNode->SetStringField(TEXT("animation_asset"), AssetToJsonAnimationPrivate::ObjectPath(AnimGraphNode->GetAnimationAsset()));
		if (const UAnimGraphNode_AssetPlayerBase* AssetPlayerNode = Cast<UAnimGraphNode_AssetPlayerBase>(AnimGraphNode))
		{
			TArray<UAnimationAsset*> ReferredAssets;
			AssetPlayerNode->GetAllAnimationSequencesReferred(ReferredAssets);

			TArray<TSharedPtr<FJsonValue>> Assets;
			for (const UAnimationAsset* Asset : ReferredAssets)
			{
				TSharedPtr<FJsonObject> JsonAsset = MakeShared<FJsonObject>();
				JsonAsset->SetStringField(TEXT("path"), AssetToJsonAnimationPrivate::ObjectPath(Asset));
				JsonAsset->SetStringField(TEXT("class"), AssetToJsonAnimationPrivate::ClassPath(Asset));
				Assets.Add(MakeShared<FJsonValueObject>(JsonAsset));
			}
			JsonAnimNode->SetArrayField(TEXT("referred_animation_assets"), Assets);
		}

		if (const UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(AnimGraphNode))
		{
			JsonAnimNode->SetStringField(TEXT("state_machine_name"), const_cast<UAnimGraphNode_StateMachineBase*>(StateMachineNode)->GetStateMachineName());
			JsonAnimNode->SetObjectField(TEXT("state_machine"), AssetToJsonAnimationPrivate::ExportAnimationStateMachine(StateMachineNode->EditorStateMachineGraph));
		}

		if (const UAnimGraphNode_LinkedAnimGraph* LinkedAnimGraphNode = Cast<UAnimGraphNode_LinkedAnimGraph>(AnimGraphNode))
		{
			JsonAnimNode->SetObjectField(TEXT("linked_anim_graph"), AssetToJsonAnimationPrivate::ExportLinkedAnimGraph(LinkedAnimGraphNode));
		}

		if (const UAnimGraphNode_LinkedAnimLayer* LinkedAnimLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(AnimGraphNode))
		{
			JsonAnimNode->SetObjectField(TEXT("linked_anim_layer"), AssetToJsonAnimationPrivate::ExportLinkedAnimLayer(LinkedAnimLayerNode));
		}

		if (const UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode = Cast<UAnimGraphNode_LinkedInputPose>(AnimGraphNode))
		{
			JsonAnimNode->SetObjectField(TEXT("linked_input_pose"), AssetToJsonAnimationPrivate::ExportLinkedInputPose(LinkedInputPoseNode));
		}

		return JsonAnimNode;
	}
}
