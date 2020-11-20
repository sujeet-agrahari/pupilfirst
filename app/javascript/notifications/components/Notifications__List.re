let str = React.string;

open Notifications__Types;

let t = I18n.t(~scope="components.Notifications__List");

type event = [ | `topic_created | `topic_edited];

type status = [ | `unread | `read];

let eventName = event => {
  switch (event) {
  | `topic_created => t("filter.events.topic_created_text")
  | `topic_edited => t("filter.events.topic_edited_text")
  };
};

module MarkAllNotificationsQuery = [%graphql
  {|
  mutation MarkAllNotificationsMutation {
    markAllNotifications {
      success
    }
  }
|}
];

type filter = {
  event: option(event),
  title: option(string),
  status: option(status),
};

type state = {
  loading: Loading.t,
  entries: Entries.t,
  filterString: string,
  filter,
  saving: bool,
  totalEntriesCount: int,
};

type action =
  | SetSearchString(string)
  | UnsetSearchString
  | UpdateFilterString(string)
  | LoadNotifications(option(string), bool, array(Entry.t), int)
  | BeginLoadingMore
  | BeginReloading
  | ClearStatus
  | SetStatus(status)
  | SetEvent(event)
  | ClearEvent
  | SetSaving
  | ClearSaving
  | MarkAllNotifications
  | MarkNotification(string);

let updateNotification = (id, notifications) => {
  notifications
  |> Js.Array.map(entry =>
       Entry.id(entry) == id ? Entry.markAsRead(entry) : entry
     );
};

let markAllNotifications = notifications => {
  notifications
  |> Js.Array.map(entry =>
       Entry.readAt(entry)->Belt.Option.isNone
         ? Entry.markAsRead(entry) : entry
     );
};

let reducer = (state, action) => {
  switch (action) {
  | SetSearchString(string) => {
      ...state,
      filter: {
        ...state.filter,
        title: Some(string),
      },
      filterString: "",
    }
  | UnsetSearchString => {
      ...state,
      filterString: "",
      filter: {
        ...state.filter,
        title: None,
      },
    }
  | UpdateFilterString(filterString) => {...state, filterString}
  | LoadNotifications(endCursor, hasNextPage, newTopics, totalEntriesCount) =>
    let updatedTopics =
      switch (state.loading) {
      | LoadingMore =>
        newTopics |> Array.append(state.entries |> Entries.toArray)
      | Reloading => newTopics
      | NotLoading => newTopics
      };

    {
      ...state,
      entries:
        switch (hasNextPage, endCursor) {
        | (_, None)
        | (false, Some(_)) => FullyLoaded(updatedTopics)
        | (true, Some(cursor)) => PartiallyLoaded(updatedTopics, cursor)
        },
      loading: NotLoading,
      totalEntriesCount,
    };
  | BeginLoadingMore => {...state, loading: LoadingMore}
  | BeginReloading => {...state, loading: Reloading}
  | SetSaving => {...state, saving: true}
  | ClearSaving => {...state, saving: false}
  | MarkNotification(id) => {
      ...state,
      entries:
        switch (state.entries) {
        | Unloaded => Unloaded
        | PartiallyLoaded(entries, cursor) =>
          PartiallyLoaded(updateNotification(id, entries), cursor)
        | FullyLoaded(entries) =>
          FullyLoaded(updateNotification(id, entries))
        },
      totalEntriesCount:
        state.filter.status->Belt.Option.isSome
          ? state.totalEntriesCount - 1 : state.totalEntriesCount,
    }

  | MarkAllNotifications => {
      ...state,
      entries:
        switch (state.entries) {
        | Unloaded => Unloaded
        | PartiallyLoaded(entries, cursor) =>
          PartiallyLoaded(markAllNotifications(entries), cursor)
        | FullyLoaded(entries) => FullyLoaded(markAllNotifications(entries))
        },
      totalEntriesCount:
        state.filter.status->Belt.Option.isSome ? 0 : state.totalEntriesCount,
      saving: false,
    }
  | SetStatus(status) => {
      ...state,
      filterString: "",
      filter: {
        ...state.filter,
        status: Some(status),
      },
    }
  | ClearStatus => {
      ...state,
      filterString: "",
      filter: {
        ...state.filter,
        status: None,
      },
    }
  | SetEvent(event) => {
      ...state,
      filterString: "",
      filter: {
        ...state.filter,
        event: Some(event),
      },
    }
  | ClearEvent => {
      ...state,
      filterString: "",
      filter: {
        ...state.filter,
        event: None,
      },
    }
  };
};

module NotificationsQuery = [%graphql
  {|
    query NotificationsFromNotificationsListQuery($search: String, $after: String, $event: NotificationEvent, $status: NotificationStatus) {
      notifications(event: $event, search: $search, first: 10, after: $after, status: $status) {
        nodes {
          actor {
            id
            name
            title
            avatarUrl
          }
          createdAt
          event
          id
          message
          notifiableId
          notifiableType
          readAt
        }
        pageInfo{
          endCursor,hasNextPage
        }
        totalCount
      }
    }
  |}
];

let markAllNotifications = (send, event) => {
  event |> ReactEvent.Mouse.preventDefault;
  send(SetSaving);
  MarkAllNotificationsQuery.make()
  |> GraphqlQuery.sendQuery
  |> Js.Promise.then_(response => {
       response##markAllNotifications##success
         ? {
           send(MarkAllNotifications);
         }
         : send(ClearSaving);
       Js.Promise.resolve();
     })
  |> Js.Promise.catch(_ => {
       send(ClearSaving);
       Js.Promise.resolve();
     })
  |> ignore;
};

let getEntries = (send, cursor, filter) => {
  NotificationsQuery.make(
    ~status=?filter.status,
    ~after=?cursor,
    ~search=?filter.title,
    ~event=?filter.event,
    (),
  )
  |> GraphqlQuery.sendQuery
  |> Js.Promise.then_(response => {
       let newNotifications =
         response##notifications##nodes
         |> Js.Array.map(topicData => Entry.makeFromJS(topicData));

       send(
         LoadNotifications(
           response##notifications##pageInfo##endCursor,
           response##notifications##pageInfo##hasNextPage,
           newNotifications,
           response##notifications##totalCount,
         ),
       );

       Js.Promise.resolve();
     })
  |> ignore;
};

let markNotification = (send, notificationId) => {
  send(MarkNotification(notificationId));
};

let reloadEntries = (state, send) => {
  send(BeginReloading);
  getEntries(send, None, state.filter);
};

let computeInitialState = () => {
  loading: NotLoading,
  entries: Unloaded,
  filterString: "",
  filter: {
    title: None,
    event: None,
    status: None,
  },
  totalEntriesCount: 0,
  saving: false,
};

let entriesList = (caption, entries, showTime, send) => {
  <div>
    <div className="text-xs text-gray-800 px-4 lg:px-8"> {str(caption)} </div>
    <div>
      {entries |> ArrayUtils.isEmpty
         ? <div
             className="flex flex-col mx-auto bg-white rounded-md border p-6 justify-center items-center">
             <FaIcon classes="fas fa-comments text-5xl text-gray-400" />
             <h4
               className="mt-3 text-base md:text-lg text-center font-semibold">
               {t("empty_notifications")->str}
             </h4>
           </div>
         : entries
           |> Js.Array.map(entry =>
                <Notifications__EntryCard
                  key={Entry.id(entry)}
                  entry
                  markNotificationCB={markNotification(send)}
                  showTime
                />
              )
           |> React.array}
    </div>
  </div>;
};

let entriesLoadedData = (totoalNotificationsCount, loadedNotificaionsCount) => {
  <div
    className="inline-block mt-2 mx-auto bg-gray-200 text-gray-800 text-xs p-2 text-center rounded font-semibold">
    {(
       totoalNotificationsCount == loadedNotificaionsCount
         ? t(
             ~variables=[|
               (
                 "total_notifications",
                 string_of_int(totoalNotificationsCount),
               ),
             |],
             "notifications_fully_loaded_text",
           )
         : t(
             ~variables=[|
               (
                 "total_notifications",
                 string_of_int(totoalNotificationsCount),
               ),
               (
                 "loaded_notifications_count",
                 string_of_int(loadedNotificaionsCount),
               ),
             |],
             "notifications_partially_loaded_text",
           )
     )
     |> str}
  </div>;
};

module Selectable = {
  type t =
    | Event(event)
    | Status(status)
    | Title(string);

  let label = s =>
    switch (s) {
    | Event(_event) => Some(t("filter.label.event"))
    | Title(_) => Some(t("filter.label.title"))
    | Status(_) => Some(t("filter.label.status"))
    };

  let value = s =>
    switch (s) {
    | Event(event) => eventName(event)
    | Title(search) => search
    | Status(status) =>
      let key =
        switch (status) {
        | `read => "read"
        | `unread => "unread"
        };

      t("filter.status." ++ key);
    };

  let searchString = s =>
    switch (s) {
    | Event(event) => t("filter.event") ++ " " ++ eventName(event)
    | Title(search) => search
    | Status(_unread) =>
      t("filter.status.read")
      ++ " "
      ++ t("filter.status.unread")
      ++ " "
      ++ t("filter.status.all")
    };

  let color = t => {
    switch (t) {
    | Event(_event) => "blue"
    | Title(_search) => "gray"
    | Status(status) =>
      switch (status) {
      | `read => "green"
      | `unread => "orange"
      }
    };
  };

  let event = event => Event(event);
  let title = search => Title(search);
  let status = status => Status(status);
};

module Multiselect = MultiselectDropdown.Make(Selectable);

let unselected = state => {
  let eventFilters =
    Selectable.event->Array.map([|`topic_created, `topic_edited|]);

  let trimmedFilterString = state.filterString |> String.trim;
  let title =
    trimmedFilterString == ""
      ? [||] : [|Selectable.title(trimmedFilterString)|];

  let status =
    state.filter.status
    ->Belt.Option.mapWithDefault([|`read, `unread|], u =>
        switch (u) {
        | `read => [|`unread|]
        | `unread => [|`read|]
        }
      )
    |> Array.map(s => Selectable.status(s));

  eventFilters |> Js.Array.concat(title) |> Js.Array.concat(status);
};

let defaultOptions = () => {
  [|`read, `unread|] |> Array.map(s => Selectable.status(s));
};

let selected = state => {
  let selectedEventFilters =
    state.filter.event
    ->Belt.Option.mapWithDefault([||], e => [|Selectable.event(e)|]);

  let selectedSearchString =
    state.filter.title
    |> OptionUtils.mapWithDefault(
         title => {[|Selectable.title(title)|]},
         [||],
       );

  let status =
    state.filter.status
    ->Belt.Option.mapWithDefault([||], u => [|Selectable.status(u)|]);

  selectedEventFilters
  |> Js.Array.concat(selectedSearchString)
  |> Js.Array.concat(status);
};

let onSelectFilter = (send, selectable) =>
  switch (selectable) {
  | Selectable.Event(event) => send(SetEvent(event))
  | Title(title) => send(SetSearchString(title))
  | Status(s) => send(SetStatus(s))
  };

let onDeselectFilter = (send, selectable) =>
  switch (selectable) {
  | Selectable.Event(_e) => send(ClearEvent)
  | Title(_title) => send(UnsetSearchString)
  | Status(_) => send(ClearStatus)
  };

let showEntries = (entries, state, send) => {
  let filteredEntries =
    state.filter.status
    ->Belt.Option.mapWithDefault(entries, u =>
        switch (u) {
        | `read =>
          Js.Array.filter(e => Entry.readAt(e)->Belt.Option.isSome, entries)
        | `unread =>
          Js.Array.filter(e => Entry.readAt(e)->Belt.Option.isNone, entries)
        }
      );

  let now = Js.Date.make();
  let entriesToday =
    Js.Array.filter(
      e =>
        Js.Date.toDateString(Entry.createdAt(e))
        == Js.Date.toDateString(now),
      filteredEntries,
    );
  let entriesEarlier =
    Js.Array.filter(
      e =>
        Js.Date.toDateString(Entry.createdAt(e))
        != Js.Date.toDateString(now),
      filteredEntries,
    );

  <div>
    {ReactUtils.nullIf(
       entriesList("Today", entriesToday, true, send),
       ArrayUtils.isEmpty(entriesToday),
     )}
    {ReactUtils.nullIf(
       entriesList("Earlier", entriesEarlier, false, send),
       ArrayUtils.isEmpty(entriesEarlier),
     )}
    <div className="text-center">
      {entriesLoadedData(
         state.totalEntriesCount,
         Array.length(filteredEntries),
       )}
    </div>
  </div>;
};

let markAllNotificationsButton = (state, send, entries) => {
  let disabled =
    Belt.Array.every(entries, e => Entry.readAt(e)->Belt.Option.isSome);

  <div className="flex w-full justify-end px-4 lg:px-8 -mb-4">
    <button
      disabled={disabled || state.saving}
      onClick={markAllNotifications(send)}
      className="inline-flex items-center font-semibold p-2 md:py-1 bg-white hover:bg-gray-300 border rounded text-xs flex-shrink-0">
      {str(t("mark_all_as_read_button"))}
    </button>
  </div>
  ->ReactUtils.nullIf(ArrayUtils.isEmpty(entries));
};

[@react.component]
let make = () => {
  let (state, send) = React.useReducer(reducer, computeInitialState());

  React.useEffect1(
    () => {
      reloadEntries(state, send);
      None;
    },
    [|state.filter|],
  );

  <div>
    <div className="pt-4 px-4 lg:px-8 bg-gray-100">
      <div className="font-bold text-xl"> {str("Notifications")} </div>
    </div>
    <div
      className="w-full bg-gray-100 border-b sticky top-0 z-30 px-4 lg:px-8 py-3">
      <label
        className="block text-tiny font-semibold uppercase pl-px text-left">
        {t("filter.input_label")->str}
      </label>
      <Multiselect
        id="filter"
        unselected={unselected(state)}
        selected={selected(state)}
        onSelect={onSelectFilter(send)}
        onDeselect={onDeselectFilter(send)}
        value={state.filterString}
        onChange={filterString => send(UpdateFilterString(filterString))}
        placeholder={t("filter.input_placeholder")}
        hint={t("filter.input_hint")}
        defaultOptions={defaultOptions()}
      />
    </div>
    <div id="entries" className="mt-4">
      {switch (state.entries) {
       | Unloaded =>
         <div className="px-2 lg:px-8">
           {SkeletonLoading.multiple(
              ~count=10,
              ~element=SkeletonLoading.card(),
            )}
         </div>
       | PartiallyLoaded(entries, cursor) =>
         <div>
           {markAllNotificationsButton(state, send, entries)}
           {showEntries(entries, state, send)}
           {switch (state.loading) {
            | LoadingMore =>
              <div className="px-2 lg:px-8">
                {SkeletonLoading.multiple(
                   ~count=3,
                   ~element=SkeletonLoading.card(),
                 )}
              </div>
            | NotLoading =>
              <div className="px-4 lg:px-8 pb-6 mt-2">
                <button
                  className="btn btn-primary-ghost cursor-pointer w-full"
                  onClick={_ => {
                    send(BeginLoadingMore);
                    getEntries(send, Some(cursor), state.filter);
                  }}>
                  {t("button_load_more") |> str}
                </button>
              </div>
            | Reloading => React.null
            }}
         </div>
       | FullyLoaded(entries) =>
         <div>
           {markAllNotificationsButton(state, send, entries)}
           {showEntries(entries, state, send)}
         </div>
       }}
    </div>
    {switch (state.entries) {
     | Unloaded => React.null

     | _ =>
       let loading =
         switch (state.loading) {
         | NotLoading => false
         | Reloading => true
         | LoadingMore => false
         };
       <LoadingSpinner loading />;
     }}
  </div>;
};