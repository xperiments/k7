import { useState } from 'react';
import {
  Disc3,
  ListMusic,
  MessageCircle,
  PanelLeftClose,
  PanelLeftOpen,
  Plug,
  RefreshCw,
  Settings as SettingsIcon,
  Sparkles,
  Waves,
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
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import {
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
  SidebarProvider,
} from '@/components/ui/sidebar';
import { DEVICE_ORIGIN } from '../config';
import { connectToDevice, getDevice, isProbing, useDeviceLink } from '../device/device';
import type { Link } from '../device/transport';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { InstallButton } from './InstallButton';
import { useDrafts } from '../playlists/drafts';
import { navigate, routeArea, routeHref, routePage, type Route } from './router';
import { PlayerBar } from './PlayerBar';

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

function DeviceMenu({
  link,
  card,
  tracks,
  reachable,
}: {
  link: Link;
  card: boolean;
  tracks: number;
  reachable: boolean;
}) {
  const [copied, setCopied] = useState(false);
  const [connectError, setConnectError] = useState<string | null>(null);
  const [connecting, setConnecting] = useState(false);
  const stream = `${DEVICE_ORIGIN}/stream`;

  const copyMcp = async () => {
    try {
      await navigator.clipboard.writeText(`${DEVICE_ORIGIN}/mcp`);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1500);
    } catch {
      /* a page without the clipboard permission simply does not copy */
    }
  };

  /* Connecting is a click: it asks for Local Network Access and reaches the player directly.
   * Nothing opens a window, so a browser that blocks popups has nothing to block. */
  const connect = async () => {
    setConnectError(null);
    setConnecting(true);
    try {
      await connectToDevice();
    } catch (error) {
      setConnectError(
        error instanceof Error ? error.message : 'Chrome blocked local network access.',
      );
    } finally {
      setConnecting(false);
    }
  };

  const rescan = () => {
    void getDevice()
      .request('rescan')
      .catch(() => {
        /* the state the menu would report is the poll's job, not this click's */
      });
  };

  const subtitle =
    connecting
      ? 'asking for local network access…'
      : isProbing()
      ? 'looking for the player…'
      : link === 'local'
      ? 'connected on your network'
      : (connectError ?? 'no player — connect');

  /* What the player reports, as opposed to how this page reaches it: the link is the
   * subtitle's fact, these are the device's (S5's first two facts, named apart). */
  const deviceFacts = !reachable
    ? 'device unreachable'
    : card
      ? `${tracks} tracks in the player`
      : 'no card in the player';

  return (
    <DropdownMenu>
      <DropdownMenuTrigger
        render={
          <button
            type="button"
            aria-label="The player"
            title={subtitle}
            className="grid size-10 shrink-0 place-items-center rounded-panel bg-transparent text-primary outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background"
          />
        }
      >
        <span aria-hidden="true" className="k7-sidebar-logo size-8" />
      </DropdownMenuTrigger>
      <DropdownMenuContent side="bottom" align="start" className="w-64">
        {/* Base UI's group is what a group label reads its context from: a bare
         * `DropdownMenuLabel` throws "MenuGroupContext is missing" and takes the popup —
         * and the page — down with it (Base UI error #31). Radix allows it; this is not
         * Radix. The error text is on the trigger's subtitle, not in here. */}
        <DropdownMenuGroup>
          <DropdownMenuLabel>
            {link === 'none' ? 'The device' : `The device · ${deviceFacts}`}
          </DropdownMenuLabel>
          {link === 'none' ? (
            <DropdownMenuItem onClick={() => void connect()}>
              <Plug aria-hidden="true" />
              {connecting ? 'Waiting for Chrome…' : 'Connect a player'}
            </DropdownMenuItem>
          ) : null}
          <DropdownMenuItem
            render={<a href={stream} target="_blank" rel="noopener noreferrer" />}
          >
            <Waves aria-hidden="true" />
            Open the live stream
          </DropdownMenuItem>
          <DropdownMenuItem
            render={<a href={`${DEVICE_ORIGIN}/library`} target="_blank" rel="noopener noreferrer" />}
          >
            <Disc3 aria-hidden="true" />
            Open the library index
          </DropdownMenuItem>
          <DropdownMenuItem onClick={() => void copyMcp()}>
            <Sparkles aria-hidden="true" />
            {copied ? 'Copied the MCP endpoint' : 'Copy the MCP endpoint'}
          </DropdownMenuItem>
          <DropdownMenuSeparator />
          <DropdownMenuItem onClick={rescan}>
            <RefreshCw aria-hidden="true" />
            Rescan the player&apos;s library
          </DropdownMenuItem>
        </DropdownMenuGroup>
      </DropdownMenuContent>
    </DropdownMenu>
  );
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
        tooltip={label}
        className={cn(
          'h-12 w-full gap-2.5 rounded-[6px] border border-foreground/45 bg-transparent p-2 text-sm text-foreground hover:bg-foreground/10 active:bg-foreground/10 data-active:bg-transparent data-active:text-foreground',
          collapsed && 'justify-center px-0',
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
  const link = useDeviceLink();
  const drafts = useDrafts();
  const card = Boolean(status?.card);
  const tracks = status?.tracks ?? 0;

  /* There is no "Now playing" entry: the bar at the bottom of every area *is* the player, so
   * a page for it would be a second place to look at the same fact. `#/player` still renders
   * the device's facts for anyone who has the address. */
  const groups = navGroups(drafts.length, tracks);

  const area = routeArea(route);

  /* The rail's width is this browser's, like the old theme: remembered so a reload does not
   * throw the workspace back open. */
  const [collapsed, setCollapsed] = useState(() => {
    try {
      return localStorage.getItem('k7.rail.v1') === 'collapsed';
    } catch {
      return false;
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

  return (
    <SidebarProvider className="h-dvh min-h-0 w-full overflow-hidden p-1">
      <div className="flex h-full w-full flex-col gap-1 rounded-panel">
        <div className="flex min-h-0 flex-1 gap-1">
          <aside
            className={cn(
              'flex shrink-0 flex-col border-r border-border',
              collapsed ? 'w-16' : 'w-52',
            )}
          >
            <header className="flex h-12 shrink-0 items-center border-b px-2">
              <DeviceMenu link={link} card={card} tracks={tracks} reachable={reachable} />
            </header>
            <div className="flex min-h-0 flex-1 flex-col gap-3 p-2">
            <div className="min-h-0 flex-1 overflow-auto">
              <Nav groups={groups} route={route} collapsed={collapsed} />
            </div>
            <div className="flex flex-col gap-0 [&>*+*]:-mt-px">
              <InstallButton compact={collapsed} />
              <a
                href={routeHref({ name: 'settings' })}
                aria-current={route.name === 'settings' ? 'page' : undefined}
                className={cn(
                  'flex h-10 w-full items-center gap-2.5 rounded-[6px] border border-foreground/45 bg-transparent p-1 pr-3 text-sm text-foreground outline-none hover:bg-foreground/10 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background',
                  collapsed && 'justify-center px-0',
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
                <span className={cn(collapsed && 'sr-only')}>Settings</span>
              </a>
            </div>
            </div>
          </aside>

          <main className="flex min-w-0 flex-1 flex-col overflow-hidden">
            <header className="flex h-12 shrink-0 items-center gap-2 border-b px-4">
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
                <Button
                  type="button"
                  variant="ghost"
                  size="icon-sm"
                  className="shrink-0"
                  aria-label={collapsed ? 'Expand the sidebar' : 'Collapse the sidebar'}
                  aria-expanded={!collapsed}
                  onClick={toggleRail}
                >
                  {collapsed ? (
                    <PanelLeftOpen aria-hidden="true" />
                  ) : (
                    <PanelLeftClose aria-hidden="true" />
                  )}
                </Button>
              </div>
            </header>

            <div className="min-h-0 flex-1 overflow-auto">{children}</div>
            <PlayerBar />
          </main>
        </div>
      </div>
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
