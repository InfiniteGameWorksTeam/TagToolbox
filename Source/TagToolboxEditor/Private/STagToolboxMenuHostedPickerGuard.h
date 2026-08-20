// Copyright Infinite Game Works. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Wraps a gameplay tag picker that is hosted inside an auto-dismissing Slate menu
 * (combo dropdown, context-menu submenu) and swallows right-clicks before the
 * picker's tag-tree rows can see them.
 *
 * The engine picker's rows open a tag-MANAGEMENT context menu (Add Sub-Tag /
 * Rename / Delete) on right-click. Pushing that menu from inside an
 * auto-dismissing menu host can create its window against a menu-stack parent
 * that is being torn down; on Windows, CreateWindowEx then fails with error 1400
 * (invalid window handle) and the editor fatal-asserts "Window Creation Failed".
 * Swallowing RMB in BOTH the tunnel phase (preview mouse-down) and on mouse-up
 * (STableRow opens the menu from mouse-UP) makes menu-hosted pickers
 * selection-only; tag management stays in Project Settings and the tag manager.
 */
class STagToolboxMenuHostedPickerGuard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STagToolboxMenuHostedPickerGuard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}
};
