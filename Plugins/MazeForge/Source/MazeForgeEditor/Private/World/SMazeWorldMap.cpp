#include "World/SMazeWorldMap.h"

#include "AssetRegistry/AssetData.h"
#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "MazeForgeCore.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

const FName SMazeWorldMap::TabId(TEXT("MazeForgeWorldMap"));

namespace
{
	/** Height of one maze schematic, in canvas units. Width follows from its aspect. */
	constexpr float NodeHeight = 190.0f;
	constexpr float NodeMinWidth = 90.0f;
	constexpr float NodeMaxWidth = 420.0f;
	constexpr float NodeGap = 28.0f;
	constexpr float Margin = 16.0f;
	constexpr float LabelHeight = 18.0f;

	/** Half the side of a transition square, and how close a click has to be to count. */
	constexpr float PointHalf = 6.0f;
	constexpr float PointGrab = 9.0f;
	constexpr float LinkGrab = 6.0f;

	const FLinearColor FrameColor(0.55f, 0.45f, 0.20f);
	const FLinearColor RoomColor(0.42f, 0.34f, 0.15f);
	const FLinearColor EntryColor(0.90f, 0.16f, 0.16f);
	const FLinearColor GateColor(0.16f, 0.42f, 0.95f);
	const FLinearColor LinkColor(0.75f, 0.75f, 0.80f);
	const FLinearColor BrokenColor(1.00f, 0.30f, 0.30f);
	const FLinearColor SelectedColor(1.00f, 0.85f, 0.25f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.78f);

	void AddRect(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry,
	             const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color,
	             float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(5);
		Points.Add(FVector2f(Min.X, Min.Y));
		Points.Add(FVector2f(Max.X, Min.Y));
		Points.Add(FVector2f(Max.X, Max.Y));
		Points.Add(FVector2f(Min.X, Max.Y));
		Points.Add(FVector2f(Min.X, Min.Y));

		FSlateDrawElement::MakeLines(Out, Layer, Geometry, MoveTemp(Points),
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	void AddSegment(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry,
	                const FVector2f& A, const FVector2f& B, const FLinearColor& Color,
	                float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(A);
		Points.Add(B);

		FSlateDrawElement::MakeLines(Out, Layer, Geometry, MoveTemp(Points),
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	/** Distance from a point to a segment. Used to decide whether a click landed on a link. */
	float DistanceToSegment(const FVector2f& P, const FVector2f& A, const FVector2f& B)
	{
		const FVector2f AB = B - A;
		const float LengthSq = AB.SizeSquared();

		if (LengthSq <= KINDA_SMALL_NUMBER)
		{
			return (P - A).Size();
		}

		const float T = FMath::Clamp(FVector2f::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f);
		return (P - (A + AB * T)).Size();
	}
}

// ===================================================================== the canvas

void SMazeWorldMapCanvas::Construct(const FArguments& InArgs)
{
	Graph = InArgs._Graph;
	OnSelectionChanged = InArgs._OnSelectionChanged;
}

FVector2f SMazeWorldMapCanvas::ToLocal(const FMazeMapNode& Node, const FVector& World)
{
	const FVector2f Size = Node.WorldMax - Node.WorldMin;

	const float U = Size.X > KINDA_SMALL_NUMBER
		? (static_cast<float>(World.X) - Node.WorldMin.X) / Size.X : 0.5f;
	const float V = Size.Y > KINDA_SMALL_NUMBER
		? (static_cast<float>(World.Z) - Node.WorldMin.Y) / Size.Y : 0.5f;

	// Z is up in the world and down on the screen, so V is subtracted from the bottom. Getting
	// this the other way round draws a maze that is upside down but perfectly plausible, which
	// is the worst kind of wrong.
	return FVector2f(
		Node.Rect.Left + U * (Node.Rect.Right - Node.Rect.Left),
		Node.Rect.Bottom - V * (Node.Rect.Bottom - Node.Rect.Top));
}

void SMazeWorldMapCanvas::RebuildLayout(const FGeometry& Geometry) const
{
	Nodes.Reset();
	Points.Reset();

	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return;
	}

	const float Available = FMath::Max(NodeMinWidth, Geometry.GetLocalSize().X - Margin * 2.0f);

	float PenX = Margin;
	float PenY = Margin;

	for (const TSoftObjectPtr<UMazeWorldManifest>& Entry : Live->Mazes)
	{
		// Resolved and not loaded: the map draws what is already in memory and does not pull a
		// dozen manifests off disk to paint a frame. A maze that is not loaded yet appears the
		// moment something else loads it — pressing Apply Changes, or opening the asset.
		UMazeWorldManifest* Manifest = Entry.Get();
		if (!Manifest || !Manifest->WorldBounds.IsValid)
		{
			continue;
		}

		FMazeMapNode Node;
		Node.Manifest = Manifest;
		Node.WorldMin = FVector2f(static_cast<float>(Manifest->WorldBounds.Min.X),
		                          static_cast<float>(Manifest->WorldBounds.Min.Z));
		Node.WorldMax = FVector2f(static_cast<float>(Manifest->WorldBounds.Max.X),
		                          static_cast<float>(Manifest->WorldBounds.Max.Z));

		const FVector2f Size = Node.WorldMax - Node.WorldMin;
		const float Aspect = Size.Y > KINDA_SMALL_NUMBER ? Size.X / Size.Y : 1.0f;
		const float Width = FMath::Clamp(NodeHeight * Aspect, NodeMinWidth, NodeMaxWidth);

		// Wrapped rather than scrolled: a row that runs off the edge hides mazes, and a map that
		// hides part of the world is not doing its one job.
		if (PenX > Margin && PenX + Width > Margin + Available)
		{
			PenX = Margin;
			PenY += NodeHeight + LabelHeight + NodeGap;
		}

		Node.Rect = FSlateRect(PenX, PenY, PenX + Width, PenY + NodeHeight);
		PenX += Width + NodeGap;

		const int32 NodeIndex = Nodes.Add(Node);

		for (const FMazeTransitionPoint& Transition : Manifest->Transitions)
		{
			FMazeMapPoint Point;
			Point.NodeIndex = NodeIndex;
			Point.Id = Transition.Id;
			Point.Role = Transition.Role;
			Point.Screen = ToLocal(Nodes[NodeIndex], Transition.Location);
			Points.Add(Point);
		}
	}
}

int32 SMazeWorldMapCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                   const FSlateRect& CullingRect,
                                   FSlateWindowElementList& OutDrawElements, int32 LayerId,
                                   const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	RebuildLayout(AllottedGeometry);

	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry();
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	UMazeWorldGraph* Live = Graph.Get();
	BrokenLinkCount = 0;

	// --- the mazes themselves

	for (const FMazeMapNode& Node : Nodes)
	{
		AddRect(OutDrawElements, LayerId, Geometry,
			FVector2f(Node.Rect.Left, Node.Rect.Top),
			FVector2f(Node.Rect.Right, Node.Rect.Bottom), FrameColor, 1.5f);

		const UMazeWorldManifest* Manifest = Node.Manifest.Get();
		if (!Manifest)
		{
			continue;
		}

		// The room grid, which is the whole of the schematic. Not the cells: the picture has to
		// say where a point is, and rooms say that at a glance where thirty thousand cells do not.
		for (const FMazeRoomEntry& Room : Manifest->Rooms)
		{
			if (!Room.WorldBounds.IsValid)
			{
				continue;
			}

			const FVector2f Min = ToLocal(Node, FVector(Room.WorldBounds.Min.X, 0.0,
				Room.WorldBounds.Min.Z));
			const FVector2f Max = ToLocal(Node, FVector(Room.WorldBounds.Max.X, 0.0,
				Room.WorldBounds.Max.Z));

			AddRect(OutDrawElements, LayerId, Geometry,
				FVector2f(FMath::Min(Min.X, Max.X), FMath::Min(Min.Y, Max.Y)),
				FVector2f(FMath::Max(Min.X, Max.X), FMath::Max(Min.Y, Max.Y)), RoomColor, 1.0f);
		}

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2f(Node.Rect.Right - Node.Rect.Left, LabelHeight),
				FSlateLayoutTransform(FVector2f(Node.Rect.Left, Node.Rect.Bottom + 3.0f))),
			FText::FromString(Manifest->GetName()), Font, ESlateDrawEffect::None, LabelColor);
	}

	// --- the links, under the squares so the squares stay clickable-looking

	if (Live)
	{
		for (int32 LinkIndex = 0; LinkIndex < Live->Links.Num(); ++LinkIndex)
		{
			const FMazeWorldLink& Link = Live->Links[LinkIndex];

			FVector2f From;
			FVector2f To;

			const bool bHasFrom = ResolveEnd(Link.FromMaze, Link.FromId, From);
			const bool bHasTo = ResolveEnd(Link.ToMaze, Link.ToId, To);

			if (!bHasFrom || !bHasTo)
			{
				++BrokenLinkCount;

				// A stub out of whichever end still exists. There is nowhere to draw the other
				// half to — that point is gone — but a count without a picture leaves "which
				// three?" unanswered, and the answer is the whole reason to look at a map.
				const FVector2f Anchor = bHasFrom ? From : To;

				if (bHasFrom || bHasTo)
				{
					TArray<FVector2f> Stub;
					Stub.Add(Anchor);
					Stub.Add(Anchor + FVector2f(0.0f, -34.0f));

					FSlateDrawElement::MakeDashedLines(OutDrawElements, LayerId + 2, Geometry,
						MoveTemp(Stub), ESlateDrawEffect::None, BrokenColor, 2.0f, 5.0f);
				}

				continue;
			}

			const bool bSelected = LinkIndex == SelectedLink;
			const FLinearColor Color = bSelected ? SelectedColor : LinkColor;
			const float Thickness = bSelected ? 2.5f : 1.5f;

			AddSegment(OutDrawElements, LayerId + 2, Geometry, From, To, Color, Thickness);

			// An arrowhead, because a line between two squares says they are joined and not
			// which way anybody walks — and one-way is the whole point of a link.
			const FVector2f Direction = (To - From).GetSafeNormal();
			if (!Direction.IsNearlyZero())
			{
				const FVector2f Side(-Direction.Y, Direction.X);
				const FVector2f Tip = To - Direction * (PointHalf + 2.0f);

				AddSegment(OutDrawElements, LayerId + 2, Geometry, Tip,
					Tip - Direction * 9.0f + Side * 5.0f, Color, Thickness);
				AddSegment(OutDrawElements, LayerId + 2, Geometry, Tip,
					Tip - Direction * 9.0f - Side * 5.0f, Color, Thickness);
			}
		}
	}

	// --- the transition squares

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FMazeMapPoint& Point = Points[Index];
		const bool bGate = Point.Role == EMazeTransitionRole::Gate;
		const bool bPending = Index == PendingGate;

		const FLinearColor Color = bPending ? SelectedColor : (bGate ? GateColor : EntryColor);

		AddRect(OutDrawElements, LayerId + 3, Geometry,
			Point.Screen - FVector2f(PointHalf, PointHalf),
			Point.Screen + FVector2f(PointHalf, PointHalf), Color, bPending ? 3.0f : 2.0f);

		// The number is the only identity a point has, and it is what goes into the Links array
		// if anybody ever fills one in by hand. Cheap to draw, and it saves opening the manifest.
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2f(40.0f, 12.0f),
				FSlateLayoutTransform(Point.Screen + FVector2f(PointHalf + 2.0f, -6.0f))),
			FText::AsNumber(Point.Id), Font, ESlateDrawEffect::None, Color);
	}

	return LayerId + 5;
}

bool SMazeWorldMapCanvas::ResolveEnd(const TSoftObjectPtr<UMazeWorldManifest>& Maze,
                                     const int32 Id, FVector2f& OutScreen) const
{
	const UMazeWorldManifest* Target = Maze.Get();
	if (!Target || Id == 0)
	{
		return false;
	}

	const FMazeMapPoint* Found = Points.FindByPredicate([&](const FMazeMapPoint& Point)
	{
		return Point.Id == Id && Nodes[Point.NodeIndex].Manifest.Get() == Target;
	});

	if (!Found)
	{
		return false;
	}

	OutScreen = Found->Screen;
	return true;
}

bool SMazeWorldMapCanvas::ResolveLink(const int32 LinkIndex, FVector2f& OutFrom,
                                      FVector2f& OutTo) const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Live->Links.IsValidIndex(LinkIndex))
	{
		return false;
	}

	const FMazeWorldLink& Link = Live->Links[LinkIndex];

	return ResolveEnd(Link.FromMaze, Link.FromId, OutFrom)
		&& ResolveEnd(Link.ToMaze, Link.ToId, OutTo);
}

int32 SMazeWorldMapCanvas::HitTestPoint(const FVector2f& Local) const
{
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if ((Points[Index].Screen - Local).Size() <= PointGrab)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 SMazeWorldMapCanvas::HitTestLink(const FVector2f& Local) const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Live->Links.Num(); ++Index)
	{
		FVector2f From;
		FVector2f To;

		if (ResolveLink(Index, From, To) && DistanceToSegment(Local, From, To) <= LinkGrab)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void SMazeWorldMapCanvas::MakeLink(const int32 FromPoint, const int32 ToPoint)
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Points.IsValidIndex(FromPoint) || !Points.IsValidIndex(ToPoint))
	{
		return;
	}

	const FMazeMapPoint& From = Points[FromPoint];
	const FMazeMapPoint& To = Points[ToPoint];

	UMazeWorldManifest* FromMaze = Nodes[From.NodeIndex].Manifest.Get();
	UMazeWorldManifest* ToMaze = Nodes[To.NodeIndex].Manifest.Get();

	if (!FromMaze || !ToMaze)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeLinkMazes", "MazeForge: Link Mazes"));
	Live->Modify();

	// A gate leads to one place. Drawing a second line out of it is not two destinations, it is
	// somebody changing their mind — so the old link goes rather than quietly winning the
	// lookup, which takes the first match and would never mention the other.
	const int32 Existing = Live->Links.IndexOfByPredicate([&](const FMazeWorldLink& Candidate)
	{
		return Candidate.FromId == From.Id && Candidate.FromMaze.Get() == FromMaze;
	});

	if (Existing != INDEX_NONE)
	{
		Live->Links.RemoveAt(Existing);

		UE_LOG(LogMazeForge, Log,
			TEXT("World map: gate %d of %s already led somewhere. The old link was replaced."),
			From.Id, *FromMaze->GetName());
	}

	FMazeWorldLink Link;
	Link.FromMaze = FromMaze;
	Link.FromId = From.Id;
	Link.ToMaze = ToMaze;
	Link.ToId = To.Id;
	Live->Links.Add(Link);

	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();

	UE_LOG(LogMazeForge, Log, TEXT("World map: %s gate %d now leads to %s entry %d."),
		*FromMaze->GetName(), From.Id, *ToMaze->GetName(), To.Id);
}

void SMazeWorldMapCanvas::DeleteSelectedLink()
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Live->Links.IsValidIndex(SelectedLink))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeUnlinkMazes", "MazeForge: Remove Link"));
	Live->Modify();

	Live->Links.RemoveAt(SelectedLink);
	SelectedLink = INDEX_NONE;

	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();
}

namespace
{
	/**
	 *  Whether we can positively say this end of a link no longer exists.
	 *
	 *  The distinction is the whole safety of the button: a manifest that is not loaded yet
	 *  answers "I don't know", and not knowing must never count as gone.
	 */
	bool IsEndDefinitelyGone(const TSoftObjectPtr<UMazeWorldManifest>& Maze, const int32 Id)
	{
		const UMazeWorldManifest* Loaded = Maze.Get();
		return Loaded != nullptr && Loaded->FindTransition(Id) == nullptr;
	}

	bool IsLinkDefinitelyDead(const FMazeWorldLink& Link)
	{
		return IsEndDefinitelyGone(Link.FromMaze, Link.FromId)
			|| IsEndDefinitelyGone(Link.ToMaze, Link.ToId);
	}
}

int32 SMazeWorldMapCanvas::CountDeadLinks() const
{
	const UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FMazeWorldLink& Link : Live->Links)
	{
		Count += IsLinkDefinitelyDead(Link) ? 1 : 0;
	}

	return Count;
}

void SMazeWorldMapCanvas::RemoveDeadLinks()
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return;
	}

	const FScopedTransaction Transaction(
		LOCTEXT("MazeRemoveDeadLinks", "MazeForge: Remove Dead Links"));
	Live->Modify();

	const int32 Removed = Live->Links.RemoveAll([](const FMazeWorldLink& Link)
	{
		return IsLinkDefinitelyDead(Link);
	});

	if (Removed > 0)
	{
		SelectedLink = INDEX_NONE;
		PendingGate = INDEX_NONE;

		Live->MarkPackageDirty();
		Live->NotifyGraphChanged();

		UE_LOG(LogMazeForge, Log,
			TEXT("World map: %d dead links removed. %d left."), Removed, Live->Links.Num());
	}
}

FReply SMazeWorldMapCanvas::OnMouseButtonDown(const FGeometry& MyGeometry,
                                              const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// Taken apart rather than converted: AbsoluteToLocal hands back a type that converts to
	// both the float and the double vector, and letting the compiler pick between them is how a
	// silent narrowing warning becomes an error on somebody else's machine.
	const FVector2D Absolute = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Local(static_cast<float>(Absolute.X), static_cast<float>(Absolute.Y));

	const int32 Hit = HitTestPoint(Local);

	if (Hit != INDEX_NONE)
	{
		SelectedLink = INDEX_NONE;

		if (Points[Hit].Role == EMazeTransitionRole::Gate)
		{
			// Clicking another gate while one is pending changes the mind rather than doing
			// nothing: the alternative is a click that silently achieves neither.
			PendingGate = (PendingGate == Hit) ? INDEX_NONE : Hit;
		}
		else if (PendingGate != INDEX_NONE)
		{
			MakeLink(PendingGate, Hit);
			PendingGate = INDEX_NONE;
		}
	}
	else
	{
		const int32 Link = HitTestLink(Local);
		SelectedLink = Link;
		PendingGate = INDEX_NONE;
	}

	OnSelectionChanged.ExecuteIfBound();

	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SMazeWorldMapCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Delete || KeyEvent.GetKey() == EKeys::BackSpace)
	{
		DeleteSelectedLink();
		OnSelectionChanged.ExecuteIfBound();
		return FReply::Handled();
	}

	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		PendingGate = INDEX_NONE;
		SelectedLink = INDEX_NONE;
		OnSelectionChanged.ExecuteIfBound();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FText SMazeWorldMapCanvas::GetStatusText() const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return LOCTEXT("MapNoGraph", "Pick a Maze World Graph above.");
	}

	if (Nodes.Num() == 0)
	{
		return LOCTEXT("MapNoMazes",
			"No mazes to draw. Add their manifests above — and note that a maze only appears "
			"once it has been built, because the map is drawn from the manifest.");
	}

	if (PendingGate != INDEX_NONE && Points.IsValidIndex(PendingGate))
	{
		return FText::Format(
			LOCTEXT("MapPending", "Gate {0} is waiting — click a red square to say where it leads. "
			                      "Escape cancels."),
			FText::AsNumber(Points[PendingGate].Id));
	}

	if (SelectedLink != INDEX_NONE && Live->Links.IsValidIndex(SelectedLink))
	{
		const FMazeWorldLink& Link = Live->Links[SelectedLink];

		return FText::Format(
			LOCTEXT("MapSelected", "Link {0} → {1} selected. Delete removes it."),
			FText::AsNumber(Link.FromId), FText::AsNumber(Link.ToId));
	}

	if (BrokenLinkCount > 0)
	{
		// Said out loud and not merely drawn dashed, because this is the failure the id-as-key
		// design buys: delete a point and draw it again and it is a new point with a new number.
		return FText::Format(
			LOCTEXT("MapBroken",
				"{0} of {1} links lead nowhere — one of their ends no longer exists. Usually the "
				"point was deleted and drawn again, which gives it a new number. Draw them again."),
			FText::AsNumber(BrokenLinkCount), FText::AsNumber(Live->Links.Num()));
	}

	return FText::Format(
		LOCTEXT("MapIdle", "{0} mazes, {1} links. Click a blue gate, then a red entry."),
		FText::AsNumber(Nodes.Num()), FText::AsNumber(Live->Links.Num()));
}

// ===================================================================== the window

void SMazeWorldMap::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("MapGraphLabel", "World Graph"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMazeWorldGraph::StaticClass())
				.ObjectPath(this, &SMazeWorldMap::GetGraphPath)
				.OnObjectChanged(this, &SMazeWorldMap::OnGraphPicked)
				.AllowClear(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("MapAddLabel", "Add maze"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMazeWorldManifest::StaticClass())
				.ObjectPath_Lambda([]() { return FString(); })
				.OnObjectChanged(this, &SMazeWorldMap::OnManifestPicked)
				.AllowClear(false)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.Padding(0.0f)
			[
				SAssignNew(Canvas, SMazeWorldMapCanvas)
				.Graph_Lambda([this]() { return Graph.Get(); })
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 0.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("MazeRemoveDeadTip",
				"Removes the links whose ends no longer exist. A link into a maze that is not "
				"loaded is left alone — not being able to see it is not the same as it being gone."))
			.IsEnabled_Lambda([this]()
			{
				return Canvas.IsValid() && Canvas->CountDeadLinks() > 0;
			})
			.OnClicked_Lambda([this]()
			{
				if (Canvas.IsValid())
				{
					Canvas->RemoveDeadLinks();
				}

				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const int32 Dead = Canvas.IsValid() ? Canvas->CountDeadLinks() : 0;

					return Dead > 0
						? FText::Format(LOCTEXT("MazeRemoveDeadN", "Remove {0} dead links"),
							FText::AsNumber(Dead))
						: LOCTEXT("MazeRemoveDeadNone", "No dead links");
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f)
		[
			SAssignNew(Status, STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]()
			{
				return Canvas.IsValid() ? Canvas->GetStatusText() : FText::GetEmpty();
			})
		]
	];
}

FString SMazeWorldMap::GetGraphPath() const
{
	const UMazeWorldGraph* Live = Graph.Get();
	return Live ? Live->GetPathName() : FString();
}

void SMazeWorldMap::OnGraphPicked(const FAssetData& AssetData)
{
	Graph = Cast<UMazeWorldGraph>(AssetData.GetAsset());
}

void SMazeWorldMap::OnManifestPicked(const FAssetData& AssetData)
{
	UMazeWorldGraph* Live = Graph.Get();
	UMazeWorldManifest* Manifest = Cast<UMazeWorldManifest>(AssetData.GetAsset());

	if (!Live || !Manifest)
	{
		return;
	}

	if (Live->HasMaze(Manifest))
	{
		UE_LOG(LogMazeForge, Log, TEXT("World map: %s is already on the map."),
			*Manifest->GetName());
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeAddMazeToMap", "MazeForge: Add Maze To Map"));
	Live->Modify();

	Live->Mazes.Add(Manifest);
	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();
}

#undef LOCTEXT_NAMESPACE
