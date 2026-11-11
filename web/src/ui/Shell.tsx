import { useEffect, useState } from 'react';
import {
  Disc3,
  ListMusic,
  MessageCircle,
  Settings as SettingsIcon,
} from 'lucide-react';
import { cn } from 'cn';

import {
  Breadcrumb,
  BreadcrumbItem,
  BreadcrumbList,
  BreadcrumbPage,
  BreadcrumbSeparator,
} from '@/components/ui/breadcrumb';
import { Button } from '@/components/ui/button';
import {
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
} from '@/components/ui/sidebar';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { InstallButton } from './InstallButton';
import { useDrafts } from '../playlists/drafts';
import { navigate, routeArea, routeHref, routePage, type Route } from './router';
import { PlayerBar } from './PlayerBar';
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
 * was), four nav groups with count badges, and a footer that links Settings and names
 * the licence. Its "Projects" group and account menu are gone -- there are no projects
 * and no accounts, and inventing them to keep the shape would be a lie in the UI.
 */

interface NavItem {
  href: string;
  label: string;
  icon: React.ReactNode;
  /** The routes this item is the current page of. */
  names: Route['name'][];
  badge?: string;
}

interface NavGroup {
  id: string;
  label: string;
  items: NavItem[];
}

/** The four areas, and the routes that belong to each. */
function navGroups(drafts: number, tracks: number): NavGroup[] {
  return [
    {
      id: 'card',
      label: 'SD Card',
      items: [
        {
          href: routeHref({ name: 'process' }),
          label: 'Process',
          icon: <ListMusic aria-hidden="true" />,
          names: ['process'],
          badge: tracks > 0 ? String(tracks) : undefined,
        },
                {
          href: routeHref({ name: 'media' }),
          label: 'Albums',
          icon: <Disc3 aria-hidden="true" />,
          names: ['media', 'album', 'artist', 'video', 'drive'],
        },
      ],
    },
    {
      id: 'playlists',
      label: 'Playlists',
      items: [
        {
          href: routeHref({ name: 'playlists' }),
          label: 'Manage',
          icon: <ListMusic aria-hidden="true" />,
          names: ['playlists', 'playlist'],
          badge: drafts > 0 ? String(drafts) : undefined,
        },
        {
          href: routeHref({ name: 'ask' }),
          label: 'Ask',
          icon: <MessageCircle aria-hidden="true" />,
          names: ['ask'],
        },
      ],
    },
    // {
    //   id: 'media',
    //   label: 'Media',
    //   items: [
    //     {
    //       href: routeHref({ name: 'media' }),
    //       label: 'Albums',
    //       icon: <Disc3 aria-hidden="true" />,
    //       names: ['media', 'album', 'artist', 'video', 'drive'],
    //     },
    //   ],
    // },
  ];
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
          'h-12 w-full gap-2.5 rounded-[6px] border border-foreground/45 bg-transparent p-2 text-sm text-foreground hover:bg-foreground/10 active:bg-foreground/10 data-active:bg-transparent data-active:text-foreground',
          collapsed && 'justify-center px-2',
        )}
      >
        <span
          aria-hidden="true"
          className="grid size-8 shrink-0 place-items-center rounded-full border border-foreground/45 bg-transparent text-foreground group-data-[active]/menu-button:bg-secondary group-data-[active]/menu-button:text-secondary-foreground [&_svg]:size-4 [&_svg]:stroke-[1.25]"
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
  groups: NavGroup[];
  route: Route;
  collapsed: boolean;
}) {
  return (
    <nav aria-label="Areas" className="flex flex-col gap-3">
      {groups.map((group) => (
        <div key={group.id} className="flex flex-col gap-3">
          <div
            className={cn(
              'flex w-full min-w-0 items-center gap-2 [@media(max-height:560px)]:hidden',
              collapsed && 'hidden',
            )}
          >
            <span className="shrink-0 text-xs font-medium tracking-[0.12em] uppercase text-muted-foreground">
              {group.label}
            </span>
            <span aria-hidden="true" className="h-px min-w-0 flex-1 bg-foreground/20" />
          </div>
          <SidebarMenu className="gap-0 [&>li+li]:-mt-px">
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
        </div>
      ))}
    </nav>
  );
}

export function Shell({ route, children }: { route: Route; children: React.ReactNode }) {
  const { status, reachable } = useDeviceStatus();
  const drafts = useDrafts();
  const tracks = status?.tracks ?? 0;

  /* There is no "Now playing" entry: the bar at the bottom of every area *is* the player, so
   * a page for it would be a second place to look at the same fact. `#/player` still renders
   * the device's facts for anyone who has the address. */
  const groups = navGroups(drafts.length, tracks);

  const area = routeArea(route);

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

  /* The rail's width is this browser's, like the old theme: remembered so a reload does not
   * throw the workspace back open. */
  const [collapsed, setCollapsed] = useState(() => {
    try {
      const stored = localStorage.getItem('k7.rail.v1');
      /* Icons only is the resting state; expanding it is the choice. */
      return stored === null ? true : stored === 'collapsed';
    } catch {
      return true;
    }
  });

  const toggleRail = () => {
    setCollapsed((open) => {
      const next = !open;
      try {
        localStorage.setItem('k7.rail.v1', next ? 'collapsed' : 'open');
      } catch {
        /* a browser that refuses storage keeps it for this page's life */
      }
      return next;
    });
  };

  /* The drawer is always the collapsed rail; the desktop follows the remembered state. */
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
    <SidebarProvider className="h-dvh min-h-0 w-full overflow-hidden p-1">
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
              compact ? 'w-16' : 'w-52',
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
                        'flex h-12 w-full items-center gap-2.5 rounded-[6px] border border-foreground/45 bg-transparent p-2 text-sm text-foreground outline-none hover:bg-foreground/10 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background',
                        compact && 'justify-center px-2',
                      )}
                    >
                      <span
                        aria-hidden="true"
                        className={cn(
                          'grid size-8 shrink-0 place-items-center rounded-full border border-foreground/45 bg-transparent text-foreground [&_svg]:size-4 [&_svg]:stroke-[1.25]',
                          route.name === 'settings' && 'bg-secondary text-secondary-foreground',
                        )}
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
            <header className="flex h-12 shrink-0 items-center gap-2 border-b px-4">
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
                <BreadcrumbList className="flex-nowrap">
                  {area ? (
                    <>
                      <BreadcrumbItem className="min-w-0">
                        <span className="truncate text-sm text-muted-foreground">{area}</span>
                      </BreadcrumbItem>
                      <BreadcrumbSeparator />
                    </>
                  ) : null}
                  <BreadcrumbItem>
                    <BreadcrumbPage className="truncate">{routePage(route)}</BreadcrumbPage>
                  </BreadcrumbItem>
                </BreadcrumbList>
              </Breadcrumb>
              <div className="ml-auto flex items-center gap-2">
                {!reachable ? (
                  <span className="text-xs text-muted-foreground">device unreachable</span>
                ) : null}
              </div>
            </header>

            <div className="min-h-0 flex-1 overflow-auto">{children}</div>
            <PlayerBar />
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
        this shell. Process, Playlists, Media, Ask and Settings are.
      </p>
      <div>
        <Button variant="solid" size="sm" onClick={() => navigate(routeHref({ name: 'process' }))}>
          Go to Process
        </Button>
      </div>
    </div>
  );
}
