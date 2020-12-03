#include "BUITween.h"

#include "Components/Widget.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Sizebox.h"
#include "Blueprint/UserWidget.h"

TArray< FBUITweenInstance > UBUITween::ActiveInstances = TArray< FBUITweenInstance >();
TArray< FBUITweenInstance > UBUITween::InstancesToAdd = TArray< FBUITweenInstance >();
bool UBUITween::bIsInitialized = false;

void UBUITween::Startup()
{
	bIsInitialized = true;
	ActiveInstances.Empty();
	InstancesToAdd.Empty();
}


void UBUITween::Shutdown()
{
	ActiveInstances.Empty();
	InstancesToAdd.Empty();
	bIsInitialized = false;
}


FBUITweenInstance& UBUITween::Create( UWidget* pInWidget, float InDuration, float InDelay, bool bIsAdditive )
{
	// By default let's kill any existing tweens 
	if ( !bIsAdditive )
	{
		Clear( pInWidget );
	}

	FBUITweenInstance Instance( pInWidget, InDuration, InDelay );

	InstancesToAdd.Add( Instance );

	return InstancesToAdd.Last();
}


int32 UBUITween::Clear( UWidget* pInWidget )
{
	int32 NumRemoved = 0;
	for ( int32 i = ActiveInstances.Num() - 1; i >= 0; --i )
	{
		if ( ActiveInstances[ i ].GetWidget().IsValid() && ActiveInstances[ i ].GetWidget() == pInWidget )
		{
			ActiveInstances.RemoveAt( i );
			NumRemoved++;
		}
	}
	return NumRemoved;
}


void UBUITween::Update( float DeltaTime )
{
	// Reverse it so we can remove
	for ( int32 i = ActiveInstances.Num()-1; i >= 0; --i )
	{
		FBUITweenInstance& Inst = ActiveInstances[ i ];
		Inst.Update( DeltaTime );
		if ( Inst.IsComplete() )
		{
			FBUITweenInstance CompleteInst = Inst;
			ActiveInstances.RemoveAt( i );

			// We do this here outside of the instance update and after removing from active instances because we
			// don't know if the callback in the cleanup is going to trigger adding more events
			CompleteInst.DoCompleteCleanup();
		}
	}

	for ( int32 i = 0; i < InstancesToAdd.Num(); ++i )
	{
		ActiveInstances.Add( InstancesToAdd[ i ] );
	}
	InstancesToAdd.Empty();
}


bool UBUITween::GetIsTweening( UWidget* pInWidget )
{
	for ( int32 i = 0; i < ActiveInstances.Num(); ++i )
	{
		if ( ActiveInstances[ i ].GetWidget() == pInWidget )
		{
			return true;
		}
	}
	return false;
}

void FBUITweenInstance::Begin()
{
	bShouldUpdate = true;
	bHasPlayedStartEvent = false;
	bHasPlayedCompleteEvent = false;

	if ( !pWidget.IsValid() )
	{
		UE_LOG( LogTemp, Warning, TEXT( "Trying to start invalid widget" ) );
		return;
	}

	// Set all the props to the existng state
	TranslationProp.OnBegin( pWidget->RenderTransform.Translation );
	ScaleProp.OnBegin( pWidget->RenderTransform.Scale );
	OpacityProp.OnBegin( pWidget->GetRenderOpacity() );

	{
		UUserWidget* UW = Cast<UUserWidget>( pWidget );
		if ( UW )
		{
			ColorProp.OnBegin( UW->ColorAndOpacity );
		}
		UImage* UI = Cast<UImage>( pWidget );
		if ( UI )
		{
			ColorProp.OnBegin( UI->ColorAndOpacity );
		}
		UBorder* Border = Cast<UBorder>( pWidget );
		if ( Border )
		{
			ColorProp.OnBegin( Border->ContentColorAndOpacity );
		}
	}

	// Apply the starting conditions, even if we delay
	Apply( 0 );
}

void FBUITweenInstance::Update( float DeltaTime )
{
	if ( !bShouldUpdate && !bIsComplete )
	{
		return;
	}
	if ( !pWidget.IsValid() )
	{
		bIsComplete = true;
		return;
	}

	if ( Delay > 0 )
	{
		// TODO could correctly subtract from deltatime and use rmaining on alpha but meh
		Delay -= DeltaTime;
		return;
	}

	if ( !bHasPlayedStartEvent )
	{
		OnStartedDelegate.ExecuteIfBound( pWidget.Get() );
		bHasPlayedStartEvent = true;
	}

	// Tween each thingy
	Alpha += DeltaTime;
	if ( Alpha >= Duration )
	{
		Alpha = Duration;
		bIsComplete = true;
	}

	const float EasedAlpha = EasingParam.IsSet()
		? FBUIEasing::Ease( EasingType, Alpha, Duration, EasingParam.GetValue() )
		: FBUIEasing::Ease( EasingType, Alpha, Duration );

	Apply( EasedAlpha );
}

void FBUITweenInstance::Apply( float EasedAlpha )
{
	UWidget* Target = pWidget.Get();

	if ( ColorProp.IsSet() )
	{
		ColorProp.Update( EasedAlpha );
		UUserWidget* UW = Cast<UUserWidget>( Target );
		if ( UW )
		{
			UW->SetColorAndOpacity( ColorProp.CurrentValue );
		}
		UImage* UI = Cast<UImage>( Target );
		if ( UI )
		{
			UI->SetColorAndOpacity( ColorProp.CurrentValue );
		}
		UBorder* Border = Cast<UBorder>( Target );
		if ( Border )
		{
			Border->SetContentColorAndOpacity( ColorProp.CurrentValue );
		}
	}

	if ( OpacityProp.IsSet() )
	{
		OpacityProp.Update( EasedAlpha );
		Target->SetRenderOpacity( OpacityProp.CurrentValue );
	}

	// Only apply visibility changes at 0 or 1
	if ( VisibilityProp.IsSet() )
	{
		if ( VisibilityProp.Update( EasedAlpha ) )
		{
			Target->SetVisibility( VisibilityProp.CurrentValue );
		}
	}

	bool bChangedRenderTransform = false;
	FWidgetTransform CurrentTransform = Target->RenderTransform;

	if ( TranslationProp.IsSet() )
	{
		TranslationProp.Update( EasedAlpha );
		CurrentTransform.Translation = TranslationProp.CurrentValue;
		bChangedRenderTransform = true;
	}
	if ( ScaleProp.IsSet() )
	{
		ScaleProp.Update( EasedAlpha );
		CurrentTransform.Scale = ScaleProp.CurrentValue;
		bChangedRenderTransform = true;
	}
	if ( RotationProp.IsSet() )
	{
		if ( RotationProp.Update( EasedAlpha ) )
		{
			CurrentTransform.Angle = RotationProp.CurrentValue;
			bChangedRenderTransform = true;
		}
	}
	if ( CanvasPositionProp.IsSet() )
	{
		if ( CanvasPositionProp.Update( EasedAlpha ) )
		{
			UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>( pWidget->Slot );
			if ( CanvasSlot )
				CanvasSlot->SetPosition( CanvasPositionProp.CurrentValue );
		}
	}

	if ( bChangedRenderTransform )
	{
		Target->SetRenderTransform( CurrentTransform );
	}
}
