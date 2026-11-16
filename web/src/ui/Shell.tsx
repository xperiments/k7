import { useEffect, useState } from 'react';
import {
  ListMusic,
  Play,
  Settings as SettingsIcon,
  TriangleAlert,
} from 'lucide-react';
import { cn } from 'cn';

import {
  Breadcrumb,
  BreadcrumbItem,
  BreadcrumbLink,
  BreadcrumbList,
  BreadcrumbPage,
  BreadcrumbSeparator,
} from '@/components/ui/breadcrumb';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import {
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
} from '@/components/ui/sidebar';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { CardSwitcher } from './CardSwitcher';
import { CassetteGlyph, GalleryGlyph } from './glyphs';
import { InstallButton } from './InstallButton';
import { useDrafts, findDraft } from '../playlists/drafts';
import {
  NAV_GROUPS,
  navigate,
  routeHref,
  routeTitle,
  type NavItem,
  type Route,
} from './router';
import { PlayerBar } from './PlayerBar';
import { PlaylistTitle } from './PlaylistTitle';
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from '@/components/ui/tooltip';

/**
 * The shell: `c-sidebar-1`'s structure with this device's data in it.
 *
 * The template's three data blocks become: the device (where the workspace switcher
 * was), the nav groups with count badges, and a footer that links Settings and names
 * the licence. Its "Projects" group and account menu are gone -- there are no projects
 * and no accounts, and inventing them to keep the shape would be a lie in the UI.
 */

interface RailNavItem extends NavItem {
  icon: React.ReactNode;
  badge?: string;
}

interface RailNavGroup {
  id: string;
  label: string;
  items: RailNavItem[];
}

/** The glyph for one destination; the names and labels come from the router. */
function iconFor(item: NavItem): React.ReactNode {
  switch (item.names[0]) {
    case 'prepare':
      return <CassetteGlyph />;
    case 'media':
      return <GalleryGlyph />;
    case 'playlists':
      return <ListMusic aria-hidden="true" />;
    case 'player':
      return <Play aria-hidden="true" />;
    default:
      return null;
  }
}

/** The sidebar, with the draft count on Manage. */
function railGroups(drafts: number): RailNavGroup[] {
  return NAV_GROUPS.map((group) => ({
    id: group.id,
    label: group.label,
    items: group.items.map((item) => ({
      ...item,
      icon: iconFor(item),
      badge:
        item.names.includes('playlists') && drafts > 0 ? String(drafts) : undefined,
    })),
  }));
}

/** The K7 control: a pill twice the dome's width, a circular dome with the icon, a label. */
function RailItem({
  href,
  label,
  icon,
  active,
  badge,
  collapsed,
}: {
  href: string;
  label: string;
  icon: React.ReactNode;
  active: boolean;
  badge?: string;
  collapsed: boolean;
}) {
  return (
    <SidebarMenuItem>
      <SidebarMenuButton
        render={
          <a
            href={href}
            aria-label={label}
            aria-current={active ? 'page' : undefined}
          />
        }
        isActive={active}
        tooltip={collapsed ? { children: label, hidden: false } : undefined}
        className={cn(
          'relative h-12 w-full gap-1.5 rounded-[6px] border border-foreground/25 bg-transparent p-2 text-sm text-foreground hover:bg-foreground/10 active:bg-foreground/10 data-active:z-[1] data-active:border-primary data-active:bg-transparent data-active:text-primary',
          collapsed && 'justify-center px-2',
        )}
      >
        <span
          aria-hidden="true"
          className="grid size-8 shrink-0 place-items-center text-current [&_svg]:size-4 [&_svg]:stroke-[1.25]"
        >
          {icon}
        </span>
        <span className={cn('truncate', collapsed && 'sr-only')}>{label}</span>
        {badge ? (
          <span
            className={cn(
              'ml-auto text-xs font-medium tabular-nums text-primary',
              collapsed && 'sr-only',
            )}
          >
            {badge}
          </span>
        ) : null}
      </SidebarMenuButton>
    </SidebarMenuItem>
  );
}

function Nav({
  groups,
  route,
  collapsed,
}: {
  groups: RailNavGroup[];
  route: Route;
  collapsed: boolean;
}) {
  return (
    <nav aria-label="Areas" className="flex flex-col">
      {groups.map((group) => (
        /* The rail prints no group titles: the areas are the breadcrumb's job, and the
         * destinations are one column of domes split only by the gap between groups. Each
         * list still carries its area's name for assistive tech. */
        <SidebarMenu
          key={group.id}
          aria-label={group.label}
          className="gap-0 [&>li+li]:-mt-px"
        >
          {group.items.map((item) => (
            <RailItem
              key={item.href}
              href={item.href}
              label={item.label}
              icon={item.icon}
              active={item.names.includes(route.name)}
              badge={item.badge}
              collapsed={collapsed}
            />
          ))}
        </SidebarMenu>
      ))}
    </nav>
  );
}

export function Shell({ route, children }: { route: Route; children: React.ReactNode }) {
  const { reachable } = useDeviceStatus();
  const drafts = useDrafts();

  /* There is no "Now playing" entry: the bar at the bottom of every area *is* the player, so
   * a page for it would be a second place to look at the same fact. `#/player` still renders
   * the device's facts for anyone who has the address. */
  const groups = railGroups(drafts.length);
  const { area, page } = routeTitle(route);
  /* The editor's page is the list itself: the header names it and is where it is renamed, so a
   * list is named once and the page starts with its two columns (k7-look.md K17). */
  const list = route.name === 'playlist' ? findDraft(drafts, route.id) : undefined;

  /* The rail is a drawer below 768px and the standing column above it. */
  const [isMobile, setIsMobile] = useState(() =>
    typeof window === 'undefined' ? false : window.matchMedia('(max-width: 767px)').matches,
  );
  useEffect(() => {
    const media = window.matchMedia('(max-width: 767px)');
    const onChange = () => setIsMobile(media.matches);
    media.addEventListener('change', onChange);
    return () => media.removeEventListener('change', onChange);
  }, []);
  const [mobileOpen, setMobileOpen] = useState(false);

  /* Icons only, on every visit: the rail opens collapsed and expanding it lasts this page,
   * so a reload never throws the workspace back open. */
  const [collapsed, setCollapsed] = useState(true);

  const toggleRail = () => setCollapsed((open) => !open);

  /* The drawer is always the collapsed rail; the desktop follows the toggle above. */
  const compact = isMobile || collapsed;
  const railExpanded = isMobile ? mobileOpen : !collapsed;
  const railLabel = isMobile
    ? mobileOpen
      ? 'Hide the sidebar'
      : 'Show the sidebar'
    : collapsed
      ? 'Expand the sidebar'
      : 'Collapse the sidebar';
  const toggleRailForViewport = () => {
    if (isMobile) setMobileOpen((open) => !open);
    else toggleRail();
  };

  return (
    <SidebarProvider className="h-dvh min-h-0 w-full overflow-hidden">
      <TooltipProvider delay={2000}>
      <div className="relative flex h-full w-full flex-col gap-1 rounded-panel">
        {mobileOpen ? (
          <button
            type="button"
            aria-label="Close the sidebar"
            onClick={() => setMobileOpen(false)}
            className="absolute inset-0 z-30 hidden bg-background/80 max-[767px]:block"
          />
        ) : null}
        <div className="flex min-h-0 flex-1 gap-1">
          <aside
            className={cn(
              'flex shrink-0 flex-col border-r border-border',
              compact ? 'w-16' : 'w-38',
              'max-[767px]:absolute max-[767px]:inset-y-0 max-[767px]:left-0 max-[767px]:z-40 max-[767px]:bg-background',
              mobileOpen ? '' : 'max-[767px]:hidden',
            )}
          >
            {isMobile ? (
              <header className="flex h-12 shrink-0 items-center justify-center border-b px-2">
                <Tooltip>
                  <TooltipTrigger
                    render={
                      <button
                        type="button"
                        onClick={() => setMobileOpen(false)}
                        aria-label="Hide the sidebar"
                        className="grid size-9 shrink-0 place-items-center rounded-panel text-primary outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background"
                      >
                        <span aria-hidden="true" className="k7-sidebar-logo size-7" />
                      </button>
                    }
                  />
                  <TooltipContent side="right">Hide the sidebar</TooltipContent>
                </Tooltip>
              </header>
            ) : (
              <header
                className={cn(
                  'flex h-12 shrink-0 items-center border-b px-2',
                  compact && 'justify-center',
                )}
              >
                <Tooltip>
                  <TooltipTrigger
                    render={
                      <button
                        type="button"
                        onClick={toggleRail}
                        aria-label={collapsed ? 'Expand the sidebar' : 'Collapse the sidebar'}
                        aria-expanded={!collapsed}
                        className="grid size-9 shrink-0 place-items-center rounded-panel text-primary outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background"
                      >
                        <span aria-hidden="true" className="k7-sidebar-logo size-7" />
                      </button>
                    }
                  />
                  <TooltipContent side="right">
                    {collapsed ? 'Expand the sidebar' : 'Collapse the sidebar'}
                  </TooltipContent>
                </Tooltip>
              </header>
            )}
            <div className="flex min-h-0 flex-1 flex-col gap-3 p-2">
            <div className="min-h-0 flex-1 overflow-auto">
              <Nav groups={groups} route={route} collapsed={compact} />
            </div>
            <div className="flex flex-col gap-0 [&>*+*]:-mt-px">
              <InstallButton compact={compact} />
              <Tooltip disabled={!compact}>
                <TooltipTrigger
                  render={
                    <a
                      href={routeHref({ name: 'settings' })}
                      aria-current={route.name === 'settings' ? 'page' : undefined}
                      className={cn(
                        'relative flex h-12 w-full items-center gap-2.5 rounded-[6px] border border-foreground/25 bg-transparent p-2 text-sm text-foreground outline-none hover:bg-foreground/10 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background',
                        compact && 'justify-center px-2',
                        route.name === 'settings' && 'z-[1] border-primary text-primary',
                      )}
                    >
                      <span
                        aria-hidden="true"
                        className="grid size-8 shrink-0 place-items-center text-current [&_svg]:size-4 [&_svg]:stroke-[1.25]"
                      >
                        <SettingsIcon aria-hidden="true" />
                      </span>
                      <span className={cn(compact && 'sr-only')}>Settings</span>
                    </a>
                  }
                />
                <TooltipContent side="right" hidden={!collapsed}>
                  Settings
                </TooltipContent>
              </Tooltip>
            </div>
            </div>
          </aside>

          <main className="flex min-w-0 flex-1 flex-col overflow-hidden">
            <header className="flex h-12 shrink-0 items-center gap-1 border-b px-4 max-[767px]:px-2 md:gap-2">
              {isMobile ? (
                <Tooltip>
                  <TooltipTrigger
                    render={
                      <button
                        type="button"
                        onClick={toggleRailForViewport}
                        aria-label={railLabel}
                        aria-expanded={railExpanded}
                        className="grid size-9 shrink-0 place-items-center rounded-panel text-primary outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background"
                      >
                        <span aria-hidden="true" className="k7-sidebar-logo size-7" />
                      </button>
                    }
                  />
                  <TooltipContent side="bottom">{railLabel}</TooltipContent>
                </Tooltip>
              ) : null}
              <Breadcrumb className="min-w-0">
                <BreadcrumbList className="min-w-0 flex-nowrap">
                  {/* The Media Sources header names the area and the view; the card is
                      the switcher's fact, and the rail is the way back. */}
                  {area !== null ? (
                    <>
                      <BreadcrumbItem className="min-w-0 max-[767px]:hidden">
                        {/* On the editor the area is the way back to the list of lists: the
                            list's own name is the page there, and it is a control. */}
                        {route.name === 'playlist' ? (
                          <BreadcrumbLink
                            href={routeHref({ name: 'playlists' })}
                            className="truncate text-sm text-muted-foreground"
                          >
                            {area}
                          </BreadcrumbLink>
                        ) : (
                          <span className="truncate text-sm text-muted-foreground">{area}</span>
                        )}
                      </BreadcrumbItem>
                      <BreadcrumbSeparator className="max-[767px]:hidden" />
                    </>
                  ) : null}
                  <BreadcrumbItem className="min-w-0">
                    {list !== undefined ? (
                      <PlaylistTitle key={list.id} draft={list} />
                    ) : (
                      <BreadcrumbPage className="truncate">{page}</BreadcrumbPage>
                    )}
                  </BreadcrumbItem>
                </BreadcrumbList>
              </Breadcrumb>
              <div className="ml-auto flex shrink-0 items-center gap-2">
                <CardSwitcher />
                {!reachable ? (
                  <Badge
                    variant="alert"
                    size="sm"
                    role="status"
                    aria-label="device unreachable"
                    title="device unreachable"
                    className="gap-1 px-0 md:px-2"
                  >
                    <TriangleAlert aria-hidden="true" />
                    <span className="hidden md:inline">device unreachable</span>
                  </Badge>
                ) : null}
              </div>
            </header>

            <div className="min-h-0 flex-1 overflow-x-hidden overflow-y-auto">{children}</div>
            {/* Prepare is the card-in-computer workbench: the player bar is about the card
                in the player, and it would take room from the tree the user is working in. */}
            {route.name === 'prepare' ? null : <PlayerBar />}
          </main>
        </div>
      </div>
      </TooltipProvider>
    </SidebarProvider>
  );
}

/** The page a route that the shell cannot render lands on. */
export function NotFound({ path }: { path: string }) {
  return (
    <div className="flex flex-col gap-3 p-6">
      <h1 className="text-xl font-light tracking-tight">No such place</h1>
      <p className="text-sm text-muted-foreground">
        <code className="bg-muted px-1 py-0.5 font-mono text-xs">#{path}</code> is not a route in
        this shell. Sources, Playlists, Albums, IA Agent, Player and Settings are.
      </p>
      <div>
        <Button variant="solid" size="sm" onClick={() => navigate(routeHref({ name: 'prepare' }))}>
          Go to Sources
        </Button>
      </div>
    </div>
  );
}
